#pragma once

#include "shared/features/world/world_entity.h"

#include "sdk/cryengine/3dengine/c_time_of_day.h"
#include "sdk/wh/environmentmodule/c_weather_system.h"
#include "sdk/wh/rpgmodule/c_calendar.h"

#include <cstdint>

namespace KCDC::Features::World {
    class WorldService;

    /**
     * Client half of the authoritative world snapshot.
     *
     * The server keeps one of these and owns it outright: there is exactly one
     * in a session, nobody else may write it, and it carries the time of day,
     * how fast it runs, and the weather blend in progress. This half states
     * that on the native clock and weather system, and hands them back
     * untouched when the session ends.
     *
     * `WorldService` owns the grip on the natives themselves -- which calendar
     * is the mod's, the blend queued with the engine, the rain override --
     * because those outlive any one snapshot and are read from the engine's
     * own detours.
     */
    class World final: public KCDC::Shared::Entities::WorldEntity {
      public:
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** States this snapshot on the native clock and weather. */
        void Apply();

        /**
         * Lets go of the natives this replica had bound.
         *
         * With `restore`, the clock scale, the faked time of day and the
         * weather the player had before the session are put back -- but only
         * if this is still the same world, and the clock has not been wound
         * backwards under us since.
         */
        void ReleaseNative(bool restore);

      private:
        /** The natives this snapshot needs, all present or none. */
        struct NativeWorld {
            SDK::wh::rpgmodule::C_Calendar *calendar             = nullptr;
            SDK::wh::environmentmodule::C_WeatherSystem *weather = nullptr;
            SDK::CTimeOfDay *tod                                 = nullptr;

            bool IsUsable() const {
                return calendar != nullptr && weather != nullptr && tod != nullptr && tod->GetLoadedPresetCount() != 0;
            }
        };

        static NativeWorld ResolveNative();

        /** Whether this replica is bound to `native` in the current context. */
        bool IsBoundTo(const NativeWorld &native) const;

        /**
         * Binds to `native`, remembering what the player's own game had set,
         * so leaving the session can put it back.
         */
        void BindNative(const NativeWorld &native);

        /** Whether the server's clock differential is usable yet. */
        bool ServerClockKnown(std::uint64_t &localAnchor) const;

        /** Checks the snapshot did not move backwards. */
        bool AcceptSnapshot();

        /** Where the server's clock says this world should be, right now. */
        std::int64_t ProjectedWorldTime(std::uint64_t localAnchor, std::uint64_t now) const;

        /** States the projected time on the calendar, adopting it if it drifted. */
        void ApplyClock(SDK::wh::rpgmodule::C_Calendar *calendar, std::int64_t target);

        /**
         * Waits out the blend already queued with the engine.
         * Returns whether the mod is free to queue another.
         */
        bool AwaitInFlightCommand(std::uint64_t now);

        /**
         * Queues the blend this snapshot calls for, if it calls for one. A
         * target preset `tod` does not have leaves the sky as it is.
         */
        void ApplyWeather(SDK::wh::environmentmodule::C_WeatherSystem *weather, SDK::CTimeOfDay *tod, std::uint64_t localAnchor, std::uint64_t now);

        /** Hands the 3D engine the server's wind when it has changed. */
        void ApplyWind(WorldService &module);

        SDK::wh::rpgmodule::C_Calendar *_calendar             = nullptr;
        SDK::wh::environmentmodule::C_WeatherSystem *_weather = nullptr;
        std::uint64_t _context                                = 0;
        std::uint64_t _sequence                               = 0;
        std::uint64_t _generation                             = 0;
        std::int64_t _observedTime                            = 0;

        // What the player's own game had set before the session took over.
        float _offlineScale         = 15;
        float _offlineFakeHour      = 0;
        bool _offlineFake           = false;
        bool _offlineWeatherEnabled = true;

        bool _calendarAligned            = false;
        std::uint64_t _commandSequence   = 0;
        std::uint64_t _commandStartedMs  = 0;
        std::uint64_t _weatherRevision   = 0;
        std::uint64_t _windRevision      = 0;
        std::uint64_t _clockRevision     = 0;
        std::uint64_t _lastWeatherRealMs = 0;
        std::int64_t _lastWeatherWorldMs = 0;
    };
} // namespace KCDC::Features::World
