#include "world_service.h"
#include "world.h"

#include "core/application.h"
#include "features/world/world_scripting.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_client.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>

namespace KCDC::Features::World {
    namespace {
        constexpr std::uint64_t kSyncTimeoutMs    = 10000;
        constexpr std::size_t kMaxPendingCommands = 256;

        // What MakeRandomEntry is handed when the queue has never run.
        constexpr std::uint32_t kNoPreset = 0xFFFFFFFFU;
    } // namespace

    thread_local bool WorldService::CalendarWrite::_inScope = false;

    WorldService::CalendarWrite::CalendarWrite() {
        _inScope = true;
    }

    WorldService::CalendarWrite::~CalendarWrite() {
        _inScope = false;
    }

    std::uint64_t WorldService::Now() {
        return MafiaNet::GetTimeUS() / 1000;
    }

    // --- Session lifecycle ------------------------------------------------

    void WorldService::BeginSession() {
        _syncFailed     = false;
        _sessionStarted = Now();
    }

    bool WorldService::IsSessionActive() {
        return KCDC::Core::gApplication && KCDC::Core::gApplication->GetConnectionPhase() != Framework::Integrations::Client::ConnectionPhase::Disconnected;
    }

    void WorldService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Exactly one is expected: the server owns it outright and creates it
        // once. More than one means the two peers disagree about the protocol.
        World *world   = nullptr;
        unsigned count = 0;
        replication->ForEach<World>([&](World *entry) {
            world = entry;
            ++count;
        });

        if (count > 1) {
            Fail("Server supplied more than one WorldEntity.");
        }
        else if (world != nullptr) {
            world->Apply();
        }
        else if (Now() - _sessionStarted > kSyncTimeoutMs) {
            Fail("Server did not provide a WorldEntity; update both peers to the same mod version.");
        }

        // Disconnect destroys the replica; Apply must have returned first.
        if (!_syncFailed) {
            return;
        }

        const auto result = KCDC::Core::gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect();
        if (!result) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("World disconnect failed: {}", result.GetError().message);
        }
    }

    void WorldService::Reset() {
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<World>([](World *world) {
                world->ReleaseNative(true);
            });
        }

        _ownedCalendar.store(nullptr);
        _ownedWeather.store(nullptr);
        _pendingCommand.reset();

        // Nothing drives the puddle curve once the session is over, and the
        // command it names belongs to a binding the release above ended.
        _pendingRain.reset();

        // _retainedCommands and _rainBackup are deliberately left. The native
        // blend queue borrows a key out of a retained command until it runs --
        // including the restore ReleaseNative has just queued -- and that
        // restore is what _rainBackup is for.
    }

    void WorldService::Fail(const std::string &reason) {
        if (_syncFailed) {
            return;
        }

        _syncFailed = true;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("World synchronization: {}", reason);
        KCDC::Core::gApplication->GetChatBox().AddMessage("World", reason);
    }

    // --- Ownership of the natives -----------------------------------------

    void WorldService::NativeContextChanged() {
        ++_context;
        _ownedCalendar.store(nullptr);
        _ownedWeather.store(nullptr);
    }

    void WorldService::TakeOwnership(SDK::wh::rpgmodule::C_Calendar *calendar, SDK::wh::environmentmodule::C_WeatherSystem *weather) {
        _ownedCalendar.store(calendar);
        _ownedWeather.store(weather);
    }

    void WorldService::ReleaseOwnership() {
        ++_binding;
        _ownedCalendar.store(nullptr);
        _ownedWeather.store(nullptr);
        _pendingCommand.reset();
    }

    bool WorldService::AllowCalendarWrite(const SDK::wh::rpgmodule::C_Calendar *calendar) {
        if (CalendarWrite::InScope() || _ownedCalendar.load() != calendar) {
            return true;
        }
        // A sleep's own writes: StartTimeAcceleration sets the fast ratio
        // while the dial is still open, StopTimeAcceleration puts it back.
        const SDK::wh::playermodule::C_SkipTime *skipTime = _skipTime.load();
        return skipTime != nullptr && skipTime->GetMode() == SDK::wh::playermodule::E_SkipTimeMode::Sleep && (skipTime->GetState() == SDK::wh::playermodule::C_SkipTime::kOpen || skipTime->GetState() == SDK::wh::playermodule::C_SkipTime::kAccelerating);
    }

    void WorldService::NoteSkipTime(const SDK::wh::playermodule::C_SkipTime *skipTime) {
        _skipTime.store(skipTime);
    }

    bool WorldService::IsSleepRunningClock() const {
        const SDK::wh::playermodule::C_SkipTime *skipTime = _skipTime.load();
        return skipTime != nullptr && skipTime->GetMode() == SDK::wh::playermodule::E_SkipTimeMode::Sleep && skipTime->GetState() == SDK::wh::playermodule::C_SkipTime::kAccelerating;
    }

    bool WorldService::Matches(const PendingCommand &command) const {
        return command.weather == _ownedWeather.load() && command.context == _context.load() && command.binding == _binding.load();
    }

    // --- Queueing a blend with the engine ---------------------------------

    bool WorldService::QueueCommand(const std::shared_ptr<PendingCommand> &pending) {
        {
            std::scoped_lock lock(_commandsMutex);
            if (_retainedCommands.size() >= kMaxPendingCommands) {
                Fail("Too many unconsumed native weather commands; restart the game.");
                return false;
            }

            // Keyed by the very pointer the native queue will hand back: the
            // command carries the string, so the key stays alive with it.
            _retainedCommands.emplace(pending->target.c_str(), pending);
        }

        pending->weather->BlendTimeOfDay(pending->target.c_str(), pending->duration, true);
        return true;
    }

    bool WorldService::ExecuteWeatherCommand(SDK::wh::environmentmodule::BlendTimeOfDayCommand *command) {
        std::shared_ptr<PendingCommand> pending;
        {
            std::scoped_lock lock(_commandsMutex);
            if (const auto it = _retainedCommands.find(command->preset); it != _retainedCommands.end()) {
                pending = it->second;
                _retainedCommands.erase(it);
            }
        }

        // Not one of ours. Swallowed anyway while the mod owns this weather
        // system, because the game's own blends would fight the server's.
        if (!pending) {
            return command->weather == _ownedWeather.load();
        }

        // The session's parting shot: hand the rain controller back, and let
        // the engine run the blend that restores the player's own weather.
        if (pending->restoreInputs) {
            if (command->weather != _ownedWeather.load() && pending->context == _context.load()) {
                RestoreRain(command->weather);
            }
            pending->complete.store(true);
            return true;
        }

        const auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        if (pending->weather != command->weather || !Matches(*pending) || calendar == nullptr || calendar != _ownedCalendar.load()) {
            pending->complete.store(true);
            return true;
        }

        bool success = true;
        if (pending->applyWeather) {
            success = RunBlend(command, *pending);
        }
        if (success) {
            SeizeRain(command->weather, pending);
        }

        pending->success.store(success);
        pending->complete.store(true);
        return true;
    }

    /**
     * Runs the blend the server asked for, at the point it has reached.
     *
     * A blend that is still in progress cannot simply be started: it is
     * restarted from its source and then wound forward to where the world
     * clock says it should be.
     */
    bool WorldService::RunBlend(SDK::wh::environmentmodule::BlendTimeOfDayCommand *command, const PendingCommand &pending) const {
        const auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        auto *blender        = command->weather->GetTimeOfDayBlender();

        const auto time          = calendar->GetWorldTimeMilliseconds();
        const auto elapsed       = std::clamp(time - pending.start, std::int64_t {0}, pending.duration);
        blender->lastWorldTimeMs = time;

        bool success = true;
        if (pending.duration != 0 && elapsed < pending.duration) {
            success = blender->BlendTimeOfDay(pending.source.c_str(), 0, true);
            if (success) {
                success = blender->BlendTimeOfDay(pending.target.c_str(), pending.duration, true);
            }
            if (success) {
                // Forced blends have already consumed one source millisecond.
                blender->elapsedMs += elapsed - 1;
                blender->ApplyPresetBlend();
            }
        }
        else {
            success = blender->BlendTimeOfDay(pending.target.c_str(), 0, true);
        }

        if (success) {
            // ForceImmediateUpdate would reset rain wetness.
            command->weather->RebuildClouds();
        }
        return success;
    }

    // --- The rain controller ----------------------------------------------

    void WorldService::SeizeRain(SDK::wh::environmentmodule::C_WeatherSystem *weather, const std::shared_ptr<PendingCommand> &pending) {
        auto *rain = weather->GetRainController();

        if (_rainBackup.weather != weather || _rainBackup.context != pending->context) {
            _rainBackup          = {};
            _rainBackup.weather  = weather;
            _rainBackup.context  = pending->context;
            _rainBackup.disabled = rain->disableRainController;

            for (std::size_t i = 0; i < std::size(kRainInputs); ++i) {
                _rainBackup.values[i] = rain->*kRainInputs[i];
            }
        }

        rain->disableRainController = 0;
        rain->rainIntensityOverride = pending->intensity;
        rain->rainCurrentAmount     = pending->amount;
        rain->wetDryThreshold       = Shared::Entities::WorldEntity::kWetThreshold;
        rain->dirtCreationSpeed     = Shared::Entities::WorldEntity::kWetCreation;
        rain->dirtDryingSpeed       = Shared::Entities::WorldEntity::kWetDrying;
        rain->puddleCreationDelay   = Shared::Entities::WorldEntity::kPuddleDelay;
        rain->puddleDryingDelay     = Shared::Entities::WorldEntity::kPuddleDelay;
        rain->puddleCreationSpeed   = Shared::Entities::WorldEntity::kPuddleCreation;
        rain->puddleDryingSpeed     = Shared::Entities::WorldEntity::kPuddleDrying;
        rain->puddleMaskMinimum     = 0;
        rain->puddleMaskMaximum     = 1;
        rain->state004C             = 0;
        rain->state0050             = 3;

        // The next weather update seeds the puddle history from this command.
        _pendingRain = pending;
    }

    void WorldService::RestoreRain(SDK::wh::environmentmodule::C_WeatherSystem *weather) {
        if (_rainBackup.weather == weather && _rainBackup.context == _context.load()) {
            auto *rain = weather->GetRainController();

            for (std::size_t i = 0; i < std::size(kRainInputs); ++i) {
                rain->*kRainInputs[i] = _rainBackup.values[i];
            }
            rain->disableRainController = _rainBackup.disabled;
        }

        _rainBackup = {};
    }

    // --- The blend queue --------------------------------------------------

    /**
     * Refills the blend queue with the sky the server last set.
     *
     * Holding the preset the queue is leaving needs no lookup and cannot name
     * one this client does not have. The hold outlasts the clock's ceiling, so
     * the entry never elapses -- until a forced blend truncates it and trims
     * the queue behind it, which is how every server change arrives.
     */
    bool WorldService::HoldWeatherEntry(SDK::wh::environmentmodule::TimeOfDayBlenderLayout *blender, SDK::wh::environmentmodule::RandomPresetEntry *entry, std::uint32_t previousPreset) const {
        // Nothing to hold: the queue is empty at the start of a level. The
        // engine seeds it and the first snapshot replaces what it picked.
        if (previousPreset == kNoPreset) {
            return false;
        }

        if (SDK::wh::environmentmodule::C_WeatherSystem::FromTimeOfDayBlender(blender) != _ownedWeather.load()) {
            return false;
        }

        entry->preset  = previousPreset;
        entry->blendMs = 0;
        entry->holdMs  = Shared::Entities::WorldEntity::kMaxWorldTime;
        return true;
    }

    // --- The wind ---------------------------------------------------------

    /**
     * Hands the 3D engine the server's wind.
     *
     * Written once per change rather than restated like the rain inputs: the
     * game has no other writer, so this stands until the level reloads.
     */
    void WorldService::ApplyWind(const glm::vec3 &wind) {
        auto *engine = SDK::I3DEngine::GetInstance();

        if (engine == nullptr) {
            return;
        }

        const std::uint64_t context = _context.load();

        if (_windBackupContext != context) {
            // With e_Wind off this reads the origin, not what the level set.
            _windBackup        = engine->GetWind(false);
            _windBackupContext = context;
        }

        engine->SetWind(SDK::Vec3 {wind.x, wind.y, wind.z});
    }

    void WorldService::RestoreWind() {
        auto *engine = SDK::I3DEngine::GetInstance();

        if (engine != nullptr && _windBackupContext == _context.load()) {
            engine->SetWind(_windBackup);
        }

        _windBackupContext = 0;
    }

    void WorldService::BeforeWeatherUpdate(SDK::wh::environmentmodule::C_WeatherSystem *weather) {
        if (weather != _ownedWeather.load()) {
            RestoreRain(weather);
            return;
        }

        // The blender measures its own progress in world time. A clock the mod
        // has just adopted can leave it a day behind or ahead, which would
        // finish or rewind the blend in one update.
        if (const auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance()) {
            auto *blender   = weather->GetTimeOfDayBlender();
            const auto time = calendar->GetWorldTimeMilliseconds();
            if (time < blender->lastWorldTimeMs || time - blender->lastWorldTimeMs > Shared::Entities::WorldEntity::kWorldHour) {
                blender->lastWorldTimeMs = time;
            }
        }
    }

    void WorldService::AfterWeatherUpdate(SDK::wh::environmentmodule::C_WeatherSystem *weather) {
        AdvanceRain(weather);
    }

    /**
     * Seeds the puddle and wetness history the native update just reset.
     *
     * The server tracks how long it has been raining; this projects that onto
     * the engine's own puddle state so a client joining mid-storm sees the
     * ground the others do rather than starting dry.
     */
    void WorldService::AdvanceRain(SDK::wh::environmentmodule::C_WeatherSystem *weather) {
        if (!_pendingRain) {
            return;
        }

        if (!Matches(*_pendingRain)) {
            _pendingRain.reset();
            return;
        }

        if (_pendingRain->weather != weather) {
            return;
        }

        auto *rain         = weather->GetRainController();
        const bool raining = _pendingRain->intensity > Shared::Entities::WorldEntity::kWetThreshold;

        // BeginRaining initializes the native volume before its history is seeded.
        if (raining && rain->phase != 1) {
            return;
        }

        const std::uint64_t now = Now();
        const double seconds    = now > _pendingRain->localAnchor ? static_cast<double>(now - _pendingRain->localAnchor) / 1000.0 : 0;
        const double phaseEnd   = static_cast<double>(_pendingRain->phaseSeconds) + seconds;
        const double afterDelay = std::max(0.0, phaseEnd - Shared::Entities::WorldEntity::kPuddleDelay) - std::max(0.0, static_cast<double>(_pendingRain->phaseSeconds) - Shared::Entities::WorldEntity::kPuddleDelay);

        rain->params.fPuddlesAmount     = static_cast<float>(std::clamp(_pendingRain->wetness + seconds * (raining ? Shared::Entities::WorldEntity::kWetCreation : -Shared::Entities::WorldEntity::kWetDrying), 0.0, 3.0));
        rain->params.fPuddlesMaskAmount = static_cast<float>(std::clamp(_pendingRain->puddles + afterDelay * (raining ? Shared::Entities::WorldEntity::kPuddleCreation : -Shared::Entities::WorldEntity::kPuddleDrying), 0.0, 1.0));
        rain->phase                     = raining ? 1 : (rain->params.fPuddlesAmount > 0 || rain->params.fPuddlesMaskAmount > 0 ? 2 : 0);
        rain->phaseDurationSeconds      = Shared::Entities::WorldEntity::kPuddleDelay;
        rain->phaseElapsedSeconds       = static_cast<float>(std::min(phaseEnd, 86400.0));
        _pendingRain.reset();
    }

    // --- Points of interest -----------------------------------------------

    void WorldService::NotifyPOIDiscovered(const SDK::wh::rpgmodule::S_LocationId &poiId) {
        Scripting::Events::POIDiscovered(poiId.data1, poiId.data2, poiId.data3, poiId.data4);
    }

    void WorldService::NotifyLocationDiscovered(const SDK::wh::rpgmodule::S_LocationId &locationId) {
        Scripting::Events::LocationDiscovered(locationId.data1, locationId.data2, locationId.data3, locationId.data4);
    }
} // namespace KCDC::Features::World
