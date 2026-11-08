#include "world.h"
#include "world_service.h"

#include "common/module_support.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/wh/environmentmodule/c_environment_module.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>

namespace KCDC::Features::World {
    namespace {
        // How far ahead of the server the local clock may run before it is
        // yanked back rather than eased.
        constexpr std::int64_t kMaxClockLeadMs = 30000;

        // Below this the clock is left alone: correcting every millisecond
        // would fight the engine's own advance for no visible gain.
        constexpr std::int64_t kClockCorrectionMs = 50;

        // The share of the server's pace the local clock runs at while it is
        // ahead, so the server catches up to it without the clock stopping.
        constexpr float kAheadPace = 0.5F;

        constexpr std::uint64_t kSyncTimeoutMs = 10000;

        /** How long a settled blend stands before it is simply restated. */
        constexpr std::uint64_t kWeatherRefreshRealMs = 60000;
    } // namespace

    void World::DeallocReplica(MafiaNet::Connection_RM3 *) {
        ReleaseNative(true);
    }

    // --- Binding to the native world --------------------------------------

    World::NativeWorld World::ResolveNative() {
        auto *engine = SDK::I3DEngine::GetInstance();

        NativeWorld native;
        native.calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        native.weather  = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();
        native.tod      = engine != nullptr ? static_cast<SDK::CTimeOfDay *>(engine->GetTimeOfDay()) : nullptr;
        return native;
    }

    bool World::IsBoundTo(const NativeWorld &native) const {
        return _calendar == native.calendar && _weather == native.weather && _context == KCDC::Core::gApplication->GetWorldService().Context();
    }

    void World::BindNative(const NativeWorld &native) {
        ReleaseNative(false);

        _calendar              = native.calendar;
        _weather               = native.weather;
        _context               = KCDC::Core::gApplication->GetWorldService().Context();
        _offlineScale          = native.calendar->GetWorldTimeRatio();
        _offlineFake           = native.calendar->IsTimeOfDayFaked();
        _offlineFakeHour       = native.calendar->GetFakeTimeOfDayHour();
        _offlineWeatherEnabled = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()->IsWeatherUpdateEnabled();
    }

    void World::ReleaseNative(bool restore) {
        WorldService &module = KCDC::Core::gApplication->GetWorldService();
        module.ReleaseOwnership();

        // Only worth putting back if this is still the same world, and the
        // clock has not been wound backwards under us since.
        const bool restorable = restore && _context == module.Context() && _calendar != nullptr && _calendar == SDK::wh::rpgmodule::C_Calendar::GetInstance() && _calendar->GetWorldTimeMilliseconds() >= _observedTime;
        if (restorable) {
            WorldService::CalendarWrite scope;
            _calendar->SetWorldTimeRatio(_offlineScale);

            if (_offlineFake) {
                _calendar->SetFakeTimeOfDay(_offlineFakeHour);
            }
            else {
                _calendar->ClearFakeTimeOfDay();
            }

            if (_weather == SDK::wh::environmentmodule::C_WeatherSystem::GetInstance()) {
                auto pending           = std::make_shared<WorldService::PendingCommand>();
                pending->weather       = _weather;
                pending->context       = _context;
                pending->target        = targetPreset;
                pending->restoreInputs = true;
                module.QueueCommand(pending);

                SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()->SetWeatherUpdateEnabled(_offlineWeatherEnabled);
            }
        }

        if (restore) {
            module.RestoreWind();
        }

        _calendar           = nullptr;
        _weather            = nullptr;
        _sequence           = 0;
        _generation         = 0;
        _observedTime       = 0;
        _calendarAligned    = false;
        _commandSequence    = 0;
        _commandStartedMs   = 0;
        _weatherRevision    = 0;
        _windRevision       = 0;
        _clockRevision      = 0;
        _lastWeatherWorldMs = 0;
        _lastWeatherRealMs  = 0;
    }

    // --- Applying one snapshot --------------------------------------------

    void World::Apply() {
        WorldService &module = KCDC::Core::gApplication->GetWorldService();

        if (!IsSane() || ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
            module.Fail("Invalid authoritative snapshot; update both client and server.");
            return;
        }

        const NativeWorld native = ResolveNative();
        if (!native.IsUsable()) {
            ReleaseNative(false);
            return;
        }

        // A paused clock is the game's own doing -- a menu, a cutscene. The
        // mod steps back rather than fighting it, and re-binds when it runs.
        if (native.calendar->IsWorldTimePaused()) {
            if (!IsBoundTo(native)) {
                ReleaseNative(false);
            }
            return;
        }

        // Wound backwards under us means a load; that is a different world.
        const bool rewound = _calendar != nullptr && native.calendar->GetWorldTimeMilliseconds() < _observedTime;
        if (!IsBoundTo(native) || rewound) {
            BindNative(native);
        }

        std::uint64_t localAnchor = 0;
        if (!ServerClockKnown(localAnchor) || !AcceptSnapshot()) {
            return;
        }

        // Asleep, the local clock runs to the hour the player chose; pulling
        // it back meanwhile would hold the skip on its first hour. Afterwards
        // it is hours ahead, and ApplyClock adopts the server's time outright.
        if (module.IsSleepRunningClock()) {
            return;
        }

        const std::uint64_t now = WorldService::Now();
        module.TakeOwnership(native.calendar, native.weather);
        ApplyClock(native.calendar, ProjectedWorldTime(localAnchor, now));

        _calendarAligned = true;
        _observedTime    = native.calendar->GetWorldTimeMilliseconds();
        SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()->SetWeatherUpdateEnabled(true);

        // Ahead of the blend queue: the wind is not part of a command.
        ApplyWind(module);

        if (!AwaitInFlightCommand(now)) {
            return;
        }
        ApplyWeather(native.weather, native.tod, localAnchor, now);
    }

    bool World::ServerClockKnown(std::uint64_t &localAnchor) const {
        std::int64_t differential = 0;
        if (!KCDC::Common::ServerClockDifferential(differential)) {
            return false;
        }

        localAnchor = realTimeMs - differential;
        return true;
    }

    bool World::AcceptSnapshot() {
        if (_generation != 0 && (_generation != generation || sequence < _sequence)) {
            KCDC::Core::gApplication->GetWorldService().Fail("World generation or snapshot sequence moved backwards.");
            return false;
        }

        _sequence   = sequence;
        _generation = generation;
        return true;
    }

    // --- The clock --------------------------------------------------------

    std::int64_t World::ProjectedWorldTime(std::uint64_t localAnchor, std::uint64_t now) const {
        const std::uint64_t age = now > localAnchor ? now - localAnchor : 0;
        const double delta      = std::min(static_cast<double>(age) * timeScale, static_cast<double>(kMaxWorldTime - worldTimeMs));
        return worldTimeMs + static_cast<std::int64_t>(delta);
    }

    void World::ApplyClock(SDK::wh::rpgmodule::C_Calendar *calendar, std::int64_t target) {
        const std::int64_t current = calendar->GetWorldTimeMilliseconds();

        // Adopted outright the first time, and again whenever the local clock
        // has run far enough ahead that easing it back would take minutes.
        const bool adopt = !_calendarAligned || (current > target && (current - target > kMaxClockLeadMs || timeScale == 0));

        WorldService::CalendarWrite scope;

        if (calendar->IsTimeOfDayFaked()) {
            calendar->ClearFakeTimeOfDay();
        }

        // Ahead of the server without being far enough ahead to adopt: run the
        // clock slower and let the server's time catch up to it. Never stop
        // it -- the game reads a ratio of zero as time not running
        // (IsWorldTimeRunning, 0x18192DE1C) and refuses sleep, fast travel and
        // the knockout's own count on any frame that lands on it; the clock
        // only stops when the server's has (timeScale 0, adopted above).
        const float ratio = !adopt && current > target ? timeScale * kAheadPace : timeScale;
        if (calendar->GetWorldTimeRatio() != ratio) {
            calendar->SetWorldTimeRatio(ratio);
        }

        if (adopt) {
            calendar->AdoptWorldTimeMilliseconds(target);
            _weatherRevision = 0;
            _clockRevision   = 0;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Applied server Calendar: {} ms, scale {}", target, timeScale);
        }
        else if (target > current + kClockCorrectionMs) {
            calendar->SetWorldTimeMilliseconds(target);
        }
    }

    // --- The weather ------------------------------------------------------

    bool World::AwaitInFlightCommand(std::uint64_t now) {
        WorldService &module = KCDC::Core::gApplication->GetWorldService();

        const std::shared_ptr<WorldService::PendingCommand> &inFlight = module.InFlight();
        if (!inFlight) {
            return true;
        }

        if (!inFlight->complete.load()) {
            if (now - _commandStartedMs > kSyncTimeoutMs) {
                module.Fail("Native weather command timed out.");
            }
            return false;
        }

        if (!inFlight->success.load()) {
            module.Fail("Native weather command failed.");
            return false;
        }

        module.SetInFlight(nullptr);
        return true;
    }

    void World::ApplyWind(WorldService &module) {
        // Once per change; a level load clears this with the rest of the
        // binding and applies it again.
        if (_windRevision == windRevision) {
            return;
        }

        module.ApplyWind(wind);
        _windRevision = windRevision;
    }

    void World::ApplyWeather(SDK::wh::environmentmodule::C_WeatherSystem *weather, SDK::CTimeOfDay *tod, std::uint64_t localAnchor, std::uint64_t now) {
        WorldService &module = KCDC::Core::gApplication->GetWorldService();

        // A settled blend is still restated periodically, as a backstop to
        // `HoldWeatherEntry`: it covers a sky the engine seeded before the mod
        // took the weather system over.
        const bool settled      = _observedTime >= weatherStartMs + weatherDurationMs;
        const bool refresh      = settled && (_observedTime - _lastWeatherWorldMs >= kWorldHour || now - _lastWeatherRealMs >= kWeatherRefreshRealMs);
        const bool applyWeather = _weatherRevision != weatherRevision || _clockRevision != clockRevision || refresh;

        if (_commandSequence == sequence && !applyWeather) {
            return;
        }

        // Looked up only when a blend is due, not every frame. The engine's
        // own blender refuses a preset this client does not have, which would
        // fail the command: such a target leaves the sky where it is, and a
        // blend out of such a source starts at its target.
        std::uint32_t index    = 0;
        const bool blend       = applyWeather && tod->FindPresetByName(targetPreset.c_str(), index);
        const bool sourceKnown = blend && tod->FindPresetByName(sourcePreset.c_str(), index);
        if (applyWeather && !blend && _weatherRevision != weatherRevision) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Unknown weather preset {}; the sky stays as it is", targetPreset);
        }

        auto pending          = std::make_shared<WorldService::PendingCommand>();
        pending->weather      = weather;
        pending->context      = _context;
        pending->binding      = module.Binding();
        pending->localAnchor  = localAnchor;
        pending->source       = sourceKnown ? sourcePreset : targetPreset;
        pending->target       = targetPreset;
        pending->start        = weatherStartMs;
        pending->duration     = weatherDurationMs;
        pending->intensity    = rainIntensity;
        pending->amount       = rainAmount;
        pending->wetness      = wetness;
        pending->puddles      = puddles;
        pending->phaseSeconds = rainPhaseSeconds;
        pending->applyWeather = blend;

        if (!module.QueueCommand(pending)) {
            return;
        }

        module.SetInFlight(pending);
        _commandSequence  = sequence;
        _commandStartedMs = now;

        if (applyWeather) {
            _weatherRevision    = weatherRevision;
            _clockRevision      = clockRevision;
            _lastWeatherWorldMs = _observedTime;
            _lastWeatherRealMs  = now;
        }
    }
} // namespace KCDC::Features::World
