#include "world_environment_service.h"
#include "features/world/world_service.h"
#include "world_environment_values.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "sdk/cryengine/3dengine/c_time_of_day.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/wh/environmentmodule/c_environment_module.h"
#include "sdk/wh/environmentmodule/c_weather_profile_database.h"
#include "sdk/wh/environmentmodule/c_weather_system.h"
#include "sdk/wh/rpgmodule/c_calendar.h"

namespace KCDC::Features::World {
    const RainField kRainFields[] = {
        // Persistent inputs; the native update reads rather than replaces them.
        {"Rain volume amount", "wh_env_RainCurrentAmount", &SDK::wh::environmentmodule::RainControllerLayout::rainCurrentAmount, 0.0F, 1.0F},
        {"Rain intensity override", "wh_env_RainIntensityOverride", &SDK::wh::environmentmodule::RainControllerLayout::rainIntensityOverride, -1.0F, 1.0F},
        {"Rain threshold", "wh_env_RainThreshold", &SDK::wh::environmentmodule::RainControllerLayout::rainThreshold, 0.0F, 0.999F},
        {"Rain filter frequency", "wh_env_RainFilterFreq", &SDK::wh::environmentmodule::RainControllerLayout::rainFilterFrequency, 0.0F, 10.0F},
        {"Cloud density saturation", "wh_env_CloudDensitySaturationValue", &SDK::wh::environmentmodule::RainControllerLayout::cloudDensitySaturationValue, 0.0F, 1.0F},
        {"Lightning threshold", "wh_env_LightningThreshold", &SDK::wh::environmentmodule::RainControllerLayout::lightningThreshold, 0.0F, 1.0F},

        // Drops, splashes and wind.
        {"Rain wind strength", "wh_env_RainWindStrength", &SDK::wh::environmentmodule::RainControllerLayout::rainWindStrength, 0.0F, 20.0F},
        {"Drops amount", "wh_env_RainDropsAmountMul", &SDK::wh::environmentmodule::RainControllerLayout::rainDropsAmountMultiplier, 0.0F, 10.0F},
        {"Drops base speed", "wh_env_RainDropsSpeedBase", &SDK::wh::environmentmodule::RainControllerLayout::rainDropsBaseSpeed, 0.0F, 20.0F},
        {"Drops speed multiplier", "wh_env_RainDropsSpeedMul", &SDK::wh::environmentmodule::RainControllerLayout::rainDropsSpeedMultiplier, 0.0F, 20.0F},
        {"Drops wind multiplier", "wh_env_RainDropsSpeedWindMul", &SDK::wh::environmentmodule::RainControllerLayout::rainDropsWindSpeedMultiplier, 0.0F, 20.0F},
        {"Splashes amount", "wh_env_SplashesAmountMul", &SDK::wh::environmentmodule::RainControllerLayout::splashesAmountMultiplier, 0.0F, 10.0F},
        {"Puddle ripples", "wh_env_PuddlesRippleAmountMul", &SDK::wh::environmentmodule::RainControllerLayout::puddleRipplesAmountMultiplier, 0.0F, 10.0F},
        {"Rain diffuse darkening", "wh_env_RainDiffuseDarkening", &SDK::wh::environmentmodule::RainControllerLayout::rainDiffuseDarkening, 0.0F, 1.0F},
        {"Movement strength", "wh_env_RainMoveStrength", &SDK::wh::environmentmodule::RainControllerLayout::rainMovementStrength, 0.0F, 10.0F},
        {"Movement blend", "wh_env_RainMoveBlend", &SDK::wh::environmentmodule::RainControllerLayout::rainMovementBlend, 0.0F, 10.0F},

        // Wetness build-up and dry-out.
        {"Wet/dry threshold", "wh_env_WetDryThreshold", &SDK::wh::environmentmodule::RainControllerLayout::wetDryThreshold, 0.0F, 1.0F},
        {"Dirt creation speed", "wh_env_DirtCreationSpeed", &SDK::wh::environmentmodule::RainControllerLayout::dirtCreationSpeed, 0.0F, 10.0F},
        {"Dirt dry-up speed", "wh_env_DirtDryupSpeed", &SDK::wh::environmentmodule::RainControllerLayout::dirtDryingSpeed, 0.0F, 10.0F},
        {"Puddle creation delay", "wh_env_PuddleCreationDelay", &SDK::wh::environmentmodule::RainControllerLayout::puddleCreationDelay, 0.0F, 60.0F},
        {"Puddle dry-up delay", "wh_env_PuddleDryupDelay", &SDK::wh::environmentmodule::RainControllerLayout::puddleDryingDelay, 0.0F, 60.0F},
        {"Puddle creation speed", "wh_env_PuddleCreationSpeed", &SDK::wh::environmentmodule::RainControllerLayout::puddleCreationSpeed, 0.0F, 10.0F},
        {"Puddle dry-up speed", "wh_env_PuddleDryupSpeed", &SDK::wh::environmentmodule::RainControllerLayout::puddleDryingSpeed, 0.0F, 10.0F},
        {"Puddle mask minimum", "wh_env_PuddleMaskMin", &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMinimum, 0.0F, 1.0F},
        {"Puddle mask maximum", "wh_env_PuddleMaskMax", &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMaximum, 0.0F, 1.0F},
    };

    const std::size_t kRainFieldCount = sizeof(kRainFields) / sizeof(kRainFields[0]);

    const RainToggle kRainToggles[] = {
        {"Disable rain controller", "wh_env_DisableRainController", &SDK::wh::environmentmodule::RainControllerLayout::disableRainController},
        {"Rain debug draw", "wh_env_RainDebug", &SDK::wh::environmentmodule::RainControllerLayout::rainDebug},
        {"Rain capsule layers", "wh_env_RainLayers", &SDK::wh::environmentmodule::RainControllerLayout::rainLayers},
    };

    const std::size_t kRainToggleCount = sizeof(kRainToggles) / sizeof(kRainToggles[0]);

    std::size_t RainIntensityOverrideField() {
        const auto *entry = std::find_if(kRainFields, kRainFields + kRainFieldCount, [](const RainField &field) {
            return field.member == &SDK::wh::environmentmodule::RainControllerLayout::rainIntensityOverride;
        });

        return static_cast<std::size_t>(entry - kRainFields);
    }

    const CVarEntry kEnvironmentCVars[] = {
        {"wh_env_moon", "Moon updates", 0.0F, 1.0F, true},
        {"wh_env_RandomPresetDelay", "Random preset interval (game hours)", 0.0F, 600.0F, false},
        {"e_Wind", "Global wind calculations", 0.0F, 1.0F, true},

        {"e_Clouds", "Clouds", 0.0F, 1.0F, true},
        {"e_SkyBox", "Sky box", 0.0F, 1.0F, true},
        {"e_Fog", "Fog", 0.0F, 1.0F, true},
        {"e_VolumetricFog", "Volumetric fog", 0.0F, 1.0F, true},
        {"e_WindAreas", "Wind areas", 0.0F, 1.0F, true},

        {"wh_env_debugTod", "Time-of-day debug draw", 0.0F, 1.0F, true},
        {"wh_env_WeatherLayerProfileDebugDraw", "Weather layer debug draw", 0.0F, 1.0F, true},
    };

    const std::size_t kEnvironmentCVarCount = sizeof(kEnvironmentCVars) / sizeof(kEnvironmentCVars[0]);

    namespace {
        std::string ReadCryString(const SDK::CryStringT<char> &value) {
            return value.m_pData != nullptr ? std::string(value.m_pData) : std::string();
        }

        ConditionRange ReadCondition(const SDK::wh::environmentmodule::S_WeatherProfileData::S_Condition &value) {
            return {value.m_min, value.m_max};
        }

        SDK::CTimeOfDay *GetTimeOfDay() {
            auto *engine = SDK::I3DEngine::GetInstance();

            return engine != nullptr ? static_cast<SDK::CTimeOfDay *>(engine->GetTimeOfDay()) : nullptr;
        }
    } // namespace

    void WorldEnvironmentService::Push(Command command) {
        std::scoped_lock lock(_mutex);
        _pending.push_back(std::move(command));
    }

    void WorldEnvironmentService::QueueSetHourOfDay(float hour) {
        Command command;
        command.kind  = CommandKind::SetHourOfDay;
        command.value = hour;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueAdoptHourOfDay(float hour) {
        Command command;
        command.kind  = CommandKind::AdoptHourOfDay;
        command.value = hour;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSkipHours(float hours) {
        Command command;
        command.kind  = CommandKind::SkipHours;
        command.value = hours;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetWorldTimeRatio(float ratio) {
        Command command;
        command.kind  = CommandKind::SetWorldTimeRatio;
        command.value = ratio;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetFakeTimeOfDay(float hour) {
        Command command;
        command.kind  = CommandKind::SetFakeTimeOfDay;
        command.value = hour;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueClearFakeTimeOfDay() {
        Command command;
        command.kind = CommandKind::ClearFakeTimeOfDay;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetSunPos(float longitude, float latitude) {
        Command command;
        command.kind   = CommandKind::SetSunPos;
        command.value  = longitude;
        command.value2 = latitude;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetSunLinked(bool linked, float longitude, float latitude) {
        Command command;
        command.kind   = CommandKind::SetSunLinked;
        command.flag   = linked;
        command.value  = longitude;
        command.value2 = latitude;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueBlendPreset(const char *preset, int blendSeconds, bool force) {
        Command command;
        command.kind     = CommandKind::BlendPreset;
        command.text     = preset != nullptr ? preset : "";
        command.intValue = std::clamp(blendSeconds, 0, 3600);
        command.flag     = force;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueForceWeatherUpdate() {
        Command command;
        command.kind = CommandKind::ForceWeatherUpdate;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueRebuildClouds() {
        Command command;
        command.kind = CommandKind::RebuildClouds;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetWeatherUpdateEnabled(bool enabled) {
        Command command;
        command.kind = CommandKind::SetWeatherUpdateEnabled;
        command.flag = enabled;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetRainField(std::size_t index, float value) {
        Command command;
        command.kind  = CommandKind::SetRainField;
        command.index = index;
        command.value = value;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetRainToggle(std::size_t index, std::int32_t value) {
        Command command;
        command.kind     = CommandKind::SetRainToggle;
        command.index    = index;
        command.intValue = value;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueSetRainOverride(float intensity) {
        QueueSetRainField(RainIntensityOverrideField(), intensity);
    }

    void WorldEnvironmentService::QueueSetCVar(const char *name, float value) {
        Command command;
        command.kind  = CommandKind::SetCVar;
        command.text  = name != nullptr ? name : "";
        command.value = value;
        Push(std::move(command));
    }

    void WorldEnvironmentService::QueueRefreshProfiles() {
        std::scoped_lock lock(_mutex);
        _profileRefreshRequested = true;
    }

    void WorldEnvironmentService::Update() {
        std::vector<Command> commands;
        bool refreshProfiles = false;

        {
            std::scoped_lock lock(_mutex);
            commands.swap(_pending);
            refreshProfiles          = _profileRefreshRequested;
            _profileRefreshRequested = false;
        }

        for (const auto &command : commands) {
            Execute(command);
            std::scoped_lock lock(_mutex);
            ++_processedCommands;
        }

        if (refreshProfiles) {
            RefreshProfiles();
        }

        RefreshState();
    }

    void WorldEnvironmentService::Execute(const Command &command) {
        if (KCDC::Features::World::WorldService::IsSessionActive()) {
            SetLastAction("Use /world in chat to change the environment.");

            return;
        }

        char message[256];

        if (!std::isfinite(command.value) || !std::isfinite(command.value2)) {
            SetLastAction("rejected a non-finite environment value");

            return;
        }

        switch (command.kind) {
        case CommandKind::SetHourOfDay: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            constexpr std::int64_t msPerDay = SDK::wh::rpgmodule::C_Calendar::kMillisecondsPerDay;

            const std::int64_t before = calendar->GetWorldTimeMilliseconds();
            const float hour          = NormalizeHour(command.value);
            const std::int64_t target = calendar->SetHourOfDay(hour);
            const int hours           = static_cast<int>(hour);
            const int minutes         = static_cast<int>((hour - static_cast<float>(hours)) * 60.0F);
            const bool rolled         = target / msPerDay != before / msPerDay;
            std::snprintf(message, sizeof(message), "set the clock to %02d:%02d%s", hours, minutes, rolled ? " (rolled into the next day)" : "");
            SetLastAction(message);

            return;
        }

        case CommandKind::AdoptHourOfDay: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            const float hour          = NormalizeHour(command.value);
            const std::int64_t target = calendar->GetWorldDay() * SDK::wh::rpgmodule::C_Calendar::kMillisecondsPerDay + static_cast<std::int64_t>(hour * static_cast<float>(SDK::wh::rpgmodule::C_Calendar::kMillisecondsPerHour));
            calendar->AdoptWorldTimeMilliseconds(target);
            const int hours   = static_cast<int>(hour);
            const int minutes = static_cast<int>((hour - static_cast<float>(hours)) * 60.0F);
            std::snprintf(message, sizeof(message), "set the clock to %02d:%02d today", hours, minutes);
            SetLastAction(message);

            return;
        }

        case CommandKind::SkipHours: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            const float hours = std::clamp(command.value, 0.0F, 72.0F);
            const auto delta  = static_cast<std::int64_t>(hours * static_cast<float>(SDK::wh::rpgmodule::C_Calendar::kMillisecondsPerHour));
            calendar->SetWorldTimeMilliseconds(calendar->GetWorldTimeMilliseconds() + delta);
            std::snprintf(message, sizeof(message), "skipped %.2f hours", hours);
            SetLastAction(message);

            return;
        }

        case CommandKind::SetWorldTimeRatio: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            calendar->SetWorldTimeRatio(std::clamp(command.value, 0.0F, 200.0F));
            std::snprintf(message, sizeof(message), "world time ratio = %.2f", calendar->GetWorldTimeRatio());
            SetLastAction(message);

            return;
        }

        case CommandKind::SetFakeTimeOfDay: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            calendar->SetFakeTimeOfDay(NormalizeHour(command.value));
            std::snprintf(message, sizeof(message), "pinned the sky to %.2f h", calendar->GetFakeTimeOfDayHour());
            SetLastAction(message);

            return;
        }

        case CommandKind::ClearFakeTimeOfDay: {
            auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();

            if (calendar == nullptr) {
                SetLastAction("the world clock is not available yet");

                return;
            }

            calendar->ClearFakeTimeOfDay();
            SetLastAction("handed the sky back to the clock");

            return;
        }

        case CommandKind::SetSunPos: {
            auto *timeOfDay = GetTimeOfDay();

            if (timeOfDay == nullptr) {
                SetLastAction("time of day is not available yet");

                return;
            }

            timeOfDay->SetSunPos(std::clamp(command.value, -180.0F, 180.0F), std::clamp(command.value2, -180.0F, 180.0F));

            // Republish to update the renderer after changing the angles.
            timeOfDay->InvokeSetTimeRaw(timeOfDay->GetTime());
            SetLastAction("moved the sun");

            return;
        }

        case CommandKind::SetSunLinked: {
            auto *timeOfDay = GetTimeOfDay();

            if (timeOfDay == nullptr) {
                SetLastAction("time of day is not available yet");

                return;
            }

            SDK::ITimeOfDay::SEnvironmentInfo info {};
            info.sunLinkedToTimeOfDay = command.flag;
            info.sunRotationLatitude  = std::clamp(command.value2, -180.0F, 180.0F);
            info.sunRotationLongitude = std::clamp(command.value, -180.0F, 180.0F);
            timeOfDay->SetEnvironmentSettings(info);
            timeOfDay->InvokeSetTimeRaw(timeOfDay->GetTime());
            SetLastAction(command.flag ? "linked the sun to the clock" : "unlinked the sun from the clock");

            return;
        }

        case CommandKind::BlendPreset: {
            auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();

            if (weather == nullptr) {
                SetLastAction("the weather system is not available yet");

                return;
            }

            if (command.text.empty()) {
                SetLastAction("no preset selected");

                return;
            }

            auto *timeOfDay           = GetTimeOfDay();
            std::uint32_t presetIndex = 0;

            if (timeOfDay == nullptr || !timeOfDay->FindPresetByName(command.text.c_str(), presetIndex)) {
                SetLastAction("the selected preset is no longer loaded; select a current preset");

                return;
            }

            const std::string &preset = *_appliedPresets.insert(command.text).first;
            weather->BlendTimeOfDay(preset.c_str(), BlendGameMilliseconds(command.intValue), command.flag);
            std::snprintf(message, sizeof(message), "requested %s (%d game seconds%s)", preset.c_str(), command.intValue, command.flag ? ", interrupt transition" : "");
            SetLastAction(message);

            return;
        }

        case CommandKind::ForceWeatherUpdate: {
            auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();

            if (weather == nullptr) {
                SetLastAction("the weather system is not available yet");

                return;
            }

            weather->ForceImmediateUpdate();
            SetLastAction("forced an immediate environment update");

            return;
        }

        case CommandKind::RebuildClouds: {
            auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();

            if (weather == nullptr) {
                SetLastAction("the weather system is not available yet");

                return;
            }

            weather->RebuildClouds();
            SetLastAction("rebuilt the clouds");

            return;
        }

        case CommandKind::SetWeatherUpdateEnabled: {
            auto *environment = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance();

            if (environment == nullptr) {
                SetLastAction("the environment module is not available yet");

                return;
            }

            environment->SetWeatherUpdateEnabled(command.flag);
            SetLastAction(command.flag ? "weather updates on" : "weather updates suspended (rain submission and blending stop)");

            return;
        }

        case CommandKind::SetRainField: {
            auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();

            if (weather == nullptr || command.index >= kRainFieldCount) {
                SetLastAction("the weather system is not available yet");

                return;
            }

            const RainField &field = kRainFields[command.index];
            auto *rain             = weather->GetRainController();
            float value            = std::clamp(command.value, field.minimum, field.maximum);

            if (field.member == &SDK::wh::environmentmodule::RainControllerLayout::rainIntensityOverride && value < 0.0F) {
                value = -1.0F;
            }

            if ((field.member == &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMinimum && value > rain->puddleMaskMaximum) || (field.member == &SDK::wh::environmentmodule::RainControllerLayout::puddleMaskMaximum && value < rain->puddleMaskMinimum)) {
                SetLastAction("puddle mask minimum must not exceed maximum");

                return;
            }

            rain->*field.member = value;
            std::snprintf(message, sizeof(message), "%s = %.3f", field.consoleName, value);
            SetLastAction(message);

            return;
        }

        case CommandKind::SetRainToggle: {
            auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance();

            if (weather == nullptr || command.index >= kRainToggleCount) {
                SetLastAction("the weather system is not available yet");

                return;
            }

            const RainToggle &toggle                     = kRainToggles[command.index];
            const int value                              = toggle.member == &SDK::wh::environmentmodule::RainControllerLayout::rainLayers ? std::clamp(command.intValue, 1, 3) : (command.intValue != 0 ? 1 : 0);
            weather->GetRainController()->*toggle.member = value;
            std::snprintf(message, sizeof(message), "%s = %d", toggle.consoleName, value);
            SetLastAction(message);

            return;
        }

        case CommandKind::SetCVar: {
            if (!SDK::IConsole::HasCVar(command.text.c_str())) {
                std::snprintf(message, sizeof(message), "%s is not registered", command.text.c_str());
                SetLastAction(message);

                return;
            }

            const auto *entry = std::find_if(kEnvironmentCVars, kEnvironmentCVars + kEnvironmentCVarCount, [&command](const CVarEntry &item) {
                return command.text == item.name;
            });

            if (entry == kEnvironmentCVars + kEnvironmentCVarCount) {
                SetLastAction("not an environment-panel variable");

                return;
            }

            const float value = entry->isToggle ? (command.value != 0.0F ? 1.0F : 0.0F) : std::clamp(command.value, entry->minimum, entry->maximum);
            float applied     = 0.0F;

            if (SDK::IConsole::TrySetFloatCVar(command.text.c_str(), value, &applied)) {
                std::snprintf(message, sizeof(message), "%s = %.3f", command.text.c_str(), applied);
            }
            else {
                std::snprintf(message, sizeof(message), "%s refused the write (still %.3f)", command.text.c_str(), applied);
            }

            SetLastAction(message);

            return;
        }
        }
    }

    void WorldEnvironmentService::RefreshState() {
        EnvironmentState state;

        if (auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance()) {
            state.calendarAvailable = true;
            state.worldDay          = calendar->GetWorldDay();
            state.dayOfWeek         = calendar->GetWorldDayOfWeek();
            state.hourOfDay         = calendar->GetHourOfDay();
            state.worldTimeRatio    = calendar->GetWorldTimeRatio();
            state.worldTimePaused   = calendar->IsWorldTimePaused();
            state.timeOfDayFaked    = calendar->IsTimeOfDayFaked();
            state.fakeTimeOfDayHour = calendar->GetFakeTimeOfDayHour();
        }

        if (auto *timeOfDay = GetTimeOfDay()) {
            state.timeOfDayAvailable = true;
            state.time               = timeOfDay->GetTime();
            state.sunLatitude        = timeOfDay->GetSunLatitude();
            state.sunLongitude       = timeOfDay->GetSunLongitude();
            timeOfDay->GetAdvancedInfo(state.advanced);

            const auto *clock     = timeOfDay->GetClockState();
            state.paused          = clock->m_paused != 0;
            state.sunLinkedToTime = clock->m_sunLinkedToTimeOfDay != 0;

            const std::size_t presetCount = timeOfDay->GetLoadedPresetCount();
            state.presetNames.reserve(presetCount);

            for (std::size_t index = 0; index < presetCount; ++index) {
                const char *name = timeOfDay->GetPresetNameAt(index);
                state.presetNames.emplace_back(name != nullptr ? name : "");
            }

            state.currentPreset     = timeOfDay->GetCurrentPreset();
            state.blendTargetPreset = timeOfDay->GetBlendTargetPreset();
            state.presetBlendFactor = timeOfDay->GetBlendFactor();
        }

        if (auto *engine = SDK::I3DEngine::GetInstance()) {
            SDK::SRainParams params {};

            if (engine->GetRainParams(params)) {
                state.rainParamsAvailable = true;

                RainParams &rain         = state.rainParams;
                rain.worldPosition       = params.vWorldPos;
                rain.amount              = params.fAmount;
                rain.currentAmount       = params.fCurrentAmount;
                rain.radius              = params.fRadius;
                rain.diffuseDarkening    = params.fDiffuseDarkening;
                rain.dropsAmount         = params.fRainDropsAmount;
                rain.dropsSpeed          = params.fRainDropsSpeed;
                rain.dropsLighting       = params.fRainDropsLighting;
                rain.mistAmount          = params.fMistAmount;
                rain.mistHeight          = params.fMistHeight;
                rain.puddlesAmount       = params.fPuddlesAmount;
                rain.puddlesMaskAmount   = params.fPuddlesMaskAmount;
                rain.puddlesRippleAmount = params.fPuddlesRippleAmount;
                rain.splashesAmount      = params.fSplashesAmount;
                rain.ignoreVisAreas      = params.bIgnoreVisareas;
                rain.disableOcclusion    = params.bDisableOcclusion;
            }
        }

        if (auto *environment = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()) {
            state.weatherUpdateEnabled = environment->IsWeatherUpdateEnabled();
        }

        if (auto *weather = SDK::wh::environmentmodule::C_WeatherSystem::GetInstance()) {
            const auto *rain            = weather->GetRainController();
            state.weatherAvailable      = true;
            state.rainIntensity         = weather->GetCurrentRainIntensity();
            state.rainAmount            = weather->GetConfiguredRainAmount();
            state.rainIntensityOverride = rain->rainIntensityOverride;
            state.windSpeed             = weather->GetWindSpeed();

            state.rainFieldValues.resize(kRainFieldCount);

            for (std::size_t index = 0; index < kRainFieldCount; ++index) {
                state.rainFieldValues[index] = rain->*kRainFields[index].member;
            }

            state.rainToggleValues.resize(kRainToggleCount);

            for (std::size_t index = 0; index < kRainToggleCount; ++index) {
                state.rainToggleValues[index] = rain->*kRainToggles[index].member;
            }
        }

        state.cvarValues.resize(kEnvironmentCVarCount);
        state.cvarRegistered.resize(kEnvironmentCVarCount);

        for (std::size_t index = 0; index < kEnvironmentCVarCount; ++index) {
            const char *name            = kEnvironmentCVars[index].name;
            state.cvarRegistered[index] = SDK::IConsole::HasCVar(name) ? 1 : 0;
            state.cvarValues[index]     = SDK::IConsole::GetFloatCVar(name, 0.0F);
        }

        std::scoped_lock lock(_mutex);
        state.processedCommands = _processedCommands;
        state.pendingCommands   = static_cast<std::uint32_t>(_pending.size());
        _state                  = std::move(state);
    }

    void WorldEnvironmentService::RefreshProfiles() {
        std::vector<WeatherProfile> profiles;

        auto *database          = SDK::wh::environmentmodule::C_WeatherProfileDatabase::GetInstance();
        const std::size_t count = database->ObjectCount();
        const auto *records     = database->ObjectsBegin();

        profiles.reserve(count);

        for (std::size_t index = 0; index < count; ++index) {
            const auto &record = records[index];

            WeatherProfile profile;
            profile.name = ReadCryString(record.m_name);

            if (profile.name.empty()) {
                continue;
            }

            profile.temperature        = ReadCondition(record.m_temperature);
            profile.rainIntensity      = ReadCondition(record.m_rainIntensity);
            profile.windVelocity       = ReadCondition(record.m_windVelocity);
            profile.timeOfDayIntervals = record.m_timeOfDayIntervals.size();
            profiles.push_back(std::move(profile));
        }

        std::sort(profiles.begin(), profiles.end(), [](const WeatherProfile &lhs, const WeatherProfile &rhs) {
            return lhs.name < rhs.name;
        });

        std::scoped_lock lock(_mutex);
        _profiles = std::move(profiles);
    }

    void WorldEnvironmentService::SetLastAction(std::string action) {
        std::scoped_lock lock(_mutex);
        _lastAction = std::move(action);
    }

    EnvironmentState WorldEnvironmentService::GetState() const {
        std::scoped_lock lock(_mutex);
        EnvironmentState state = _state;
        state.pendingCommands  = static_cast<std::uint32_t>(_pending.size());

        return state;
    }

    std::vector<WeatherProfile> WorldEnvironmentService::GetProfiles() const {
        std::scoped_lock lock(_mutex);

        return _profiles;
    }

    std::string WorldEnvironmentService::GetLastAction() const {
        std::scoped_lock lock(_mutex);

        return _lastAction;
    }
} // namespace KCDC::Features::World
