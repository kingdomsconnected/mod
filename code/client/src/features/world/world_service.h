#pragma once

#include "core/feature_registry.h"

#include "world_population.h"

#include "shared/features/world/world_entity.h"

#include "sdk/wh/environmentmodule/c_weather_system.h"
#include "sdk/wh/playermodule/c_skip_time.h"
#include "sdk/wh/rpgmodule/c_calendar.h"
#include "sdk/wh/rpgmodule/s_location_id.h"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace KCDC::Features::World {
    /**
     * The rain controller inputs the mod drives, and puts back on release.
     *
     * Plain fields rather than a call: the engine recomputes them from the
     * weather preset every update, so driving rain means restating them.
     */
    inline constexpr float SDK::wh::environmentmodule::RainControllerLayout::*kRainInputs[] = {&SDK::wh::environmentmodule::RainControllerLayout::rainIntensityOverride, &SDK::wh::environmentmodule::RainControllerLayout::rainCurrentAmount,
        &SDK::wh::environmentmodule::RainControllerLayout::wetDryThreshold, &SDK::wh::environmentmodule::RainControllerLayout::dirtCreationSpeed, &SDK::wh::environmentmodule::RainControllerLayout::dirtDryingSpeed,
        &SDK::wh::environmentmodule::RainControllerLayout::puddleCreationDelay, &SDK::wh::environmentmodule::RainControllerLayout::puddleDryingDelay, &SDK::wh::environmentmodule::RainControllerLayout::puddleCreationSpeed,
        &SDK::wh::environmentmodule::RainControllerLayout::puddleDryingSpeed, &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMinimum, &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMaximum, &SDK::wh::environmentmodule::RainControllerLayout::state004C,
        &SDK::wh::environmentmodule::RainControllerLayout::state0050};

    /**
     * The mod's grip on the native clock and weather.
     *
     * Three things live here that no single World replica can own. The engine
     * writes its own time and weather from paths the mod detours, so there has
     * to be one place that says which calendar and weather system are
     * currently the mod's -- read from those detours, which run on whatever
     * thread the engine calls them on, hence the atomics.
     *
     * Then the weather itself: a blend cannot be applied where it is decided.
     * `C_WeatherSystem::BlendTimeOfDay` only queues, and the engine executes
     * the command later through a function the mod also detours. A command is
     * parked here between the two, keyed by the preset name pointer the native
     * queue borrows until it runs.
     *
     * Last, the rain controller. Its inputs are plain fields the engine
     * recomputes every update, so driving rain means overwriting them each
     * pass and putting the originals back when the session ends.
     */
    class WorldService final: public KCDC::Core::IFeatureService {
      public:
        /**
         * The entity-system sink that refuses the level's own actors.
         *
         * Handed to the engine once from `InstallPopulationSink`, at the end
         * of `CGame::CompleteInit`, and never withdrawn. Process-level rather
         * than session state: the sink holds nothing and the subscription
         * outlives every session, so no reset touches it.
         */
        KCDC::Features::World::PopulationSink &GetPopulationSink() {
            return _populationSink;
        }

        /** Game thread: applies the authoritative snapshot, or fails the session. */
        void Update() override;

        /** Hands the native world back and drops every binding. */
        void Reset() override;

        void BeginSession();
        static bool IsSessionActive();

        /**
         * A level changed under us. Everything bound to the old world is void:
         * the pointers are re-read, and anything still naming this context is
         * ignored when it comes back.
         */
        void NativeContextChanged() override;

        // --- Native edges, raised by this feature's own hooks ---

        /**
         * Whether the engine may write the calendar it is asking about.
         *
         * The game writes its own time from scripts and its own update; while
         * the mod owns a calendar only the mod's own writes get through.
         */
        bool AllowCalendarWrite(const SDK::wh::rpgmodule::C_Calendar *calendar);

        /**
         * The skip-time controller, as its BeginSkipTime hands it in: one
         * object for the life of the process.
         */
        void NoteSkipTime(const SDK::wh::playermodule::C_SkipTime *skipTime);

        /**
         * Whether this machine's player is sleeping through the game's own
         * time skip. Sleep carries the rest -- sleepiness, hunger, health, the
         * after-sleep buff -- by running the clock to the chosen hour, so for
         * that while the local clock is the game's: it is left to run, and
         * the server's time is adopted again once the skip has stopped. The
         * clock everyone else shares does not move.
         */
        bool IsSleepRunningClock() const;

        /**
         * The engine reached a queued blend. Returns whether the mod consumed
         * it -- the native implementation is skipped when it did.
         */
        bool ExecuteWeatherCommand(SDK::wh::environmentmodule::BlendTimeOfDayCommand *command);

        void BeforeWeatherUpdate(SDK::wh::environmentmodule::C_WeatherSystem *weather);
        void AfterWeatherUpdate(SDK::wh::environmentmodule::C_WeatherSystem *weather);

        /**
         * The blend queue has run dry and the engine is about to roll its own
         * next sky. Returns whether the mod filled `entry` in its place.
         *
         * Not a queued command, so swallowing those does not reach it: left
         * alone each client rolls a different sky once the server's blend has
         * stood for as long as its preset says.
         */
        bool HoldWeatherEntry(SDK::wh::environmentmodule::TimeOfDayBlenderLayout *blender, SDK::wh::environmentmodule::RandomPresetEntry *entry, std::uint32_t previousPreset) const;

        /** Dispatches a POI that became discovered to client scripts. */
        void NotifyPOIDiscovered(const SDK::wh::rpgmodule::S_LocationId &poiId);

        /** Dispatches a location -- a town, a district -- that became discovered to client scripts. */
        void NotifyLocationDiscovered(const SDK::wh::rpgmodule::S_LocationId &locationId);

        // --- Asked by the World replica ---

        /** A blend the mod wants the engine to run, and what to do when it does. */
        struct PendingCommand {
            SDK::wh::environmentmodule::C_WeatherSystem *weather = nullptr;
            std::uint64_t context                                = 0;
            std::uint64_t binding                                = 0;
            std::uint64_t localAnchor                            = 0;
            std::string source;
            std::string target;
            std::int64_t start    = 0;
            std::int64_t duration = 0;
            float intensity       = 0;
            float amount          = 1;
            float wetness         = 0;
            float puddles         = 0;
            float phaseSeconds    = 0;
            bool applyWeather     = false;
            bool restoreInputs    = false;
            std::atomic<bool> complete {false};
            std::atomic<bool> success {false};
        };

        /** Queues `pending` with the engine. Returns whether it was accepted. */
        bool QueueCommand(const std::shared_ptr<PendingCommand> &pending);

        /** Writes the server's wind into the 3D engine, saving the level's first. */
        void ApplyWind(const glm::vec3 &wind);

        /** Puts back the wind the engine held before the session took it. */
        void RestoreWind();

        /** Fails the session with `reason`, reported once. */
        void Fail(const std::string &reason);

        bool HasFailed() const {
            return _syncFailed;
        }

        std::uint64_t Context() const {
            return _context.load();
        }

        std::uint64_t Binding() const {
            return _binding.load();
        }

        /** Marks this calendar and weather system as the ones the mod drives. */
        void TakeOwnership(SDK::wh::rpgmodule::C_Calendar *calendar, SDK::wh::environmentmodule::C_WeatherSystem *weather);

        /** Drops the ownership and invalidates every command still in flight. */
        void ReleaseOwnership();

        /** The command the mod is waiting on the engine to run, if any. */
        const std::shared_ptr<PendingCommand> &InFlight() const {
            return _pendingCommand;
        }

        void SetInFlight(const std::shared_ptr<PendingCommand> &pending) {
            _pendingCommand = pending;
        }

        /** Milliseconds on the same clock the session is timed against. */
        static std::uint64_t Now();

        /** RAII: marks this thread's calendar writes as the mod's own. */
        class CalendarWrite final {
          public:
            CalendarWrite();
            ~CalendarWrite();

            CalendarWrite(const CalendarWrite &)            = delete;
            CalendarWrite &operator=(const CalendarWrite &) = delete;

            /** Whether this thread is inside one of the mod's own writes. */
            static bool InScope() {
                return _inScope;
            }

          private:
            // Set while this thread is inside one of the mod's own calendar
            // writes, so the write-guard detour lets it through. Thread-local
            // because the engine calls the detour on whichever thread it
            // likes, and per-thread re-entrancy rather than session state, so
            // no reset clears it.
            static thread_local bool _inScope;
        };

      private:
        /** Whether `command` still describes the world the mod is bound to. */
        bool Matches(const PendingCommand &command) const;

        /** Runs a consumed blend against the engine's own blender. */
        bool RunBlend(SDK::wh::environmentmodule::BlendTimeOfDayCommand *command, const PendingCommand &pending) const;

        /** Takes the rain controller over, saving what it held first. */
        void SeizeRain(SDK::wh::environmentmodule::C_WeatherSystem *weather, const std::shared_ptr<PendingCommand> &pending);

        /** Puts back what the rain controller held before the mod took it. */
        void RestoreRain(SDK::wh::environmentmodule::C_WeatherSystem *weather);

        /** Drives the puddle and wetness curve forward for one update. */
        void AdvanceRain(SDK::wh::environmentmodule::C_WeatherSystem *weather);

        // What the 3D engine held before the mod wrote it, and the context it
        // was read in. Apart from the rain backup: the wind is the 3D
        // engine's, which outlives the weather system and the level.
        SDK::Vec3 _windBackup {};
        std::uint64_t _windBackupContext = 0;

        // Bumped when the native world changes under us; a command naming an
        // older context is stale and ignored.
        std::atomic<std::uint64_t> _context {1};

        // Bumped when this peer lets go of the world; separates a re-bind from
        // a level change.
        std::atomic<std::uint64_t> _binding {1};

        // Read from the engine's own detours, on whatever thread it calls them.
        std::atomic<SDK::wh::rpgmodule::C_Calendar *> _ownedCalendar {nullptr};
        std::atomic<SDK::wh::environmentmodule::C_WeatherSystem *> _ownedWeather {nullptr};
        std::atomic<const SDK::wh::playermodule::C_SkipTime *> _skipTime {nullptr};

        // The native queue borrows the preset name pointer until execution, so
        // the command is keyed by exactly that pointer.
        std::mutex _commandsMutex;
        std::unordered_map<const char *, std::shared_ptr<PendingCommand>> _retainedCommands;

        std::shared_ptr<PendingCommand> _pendingCommand;
        std::shared_ptr<PendingCommand> _pendingRain;

        /** What the rain controller held before the mod overwrote it. */
        struct RainBackup {
            SDK::wh::environmentmodule::C_WeatherSystem *weather = nullptr;
            std::uint64_t context                                = 0;
            float values[std::size(kRainInputs)] {};
            std::int32_t disabled = 0;
        } _rainBackup;

        bool _syncFailed              = false;
        std::uint64_t _sessionStarted = 0;

        // Registered with the entity system once at boot and kept for the life
        // of the process; stateless, so nothing here is reset.
        KCDC::Features::World::PopulationSink _populationSink;
    };
} // namespace KCDC::Features::World
