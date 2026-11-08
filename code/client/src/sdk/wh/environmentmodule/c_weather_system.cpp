#include "c_weather_system.h"

#include "../../patterns.h"
#include "c_environment_module.h"
#include <utils/hooking/hooking.h>

namespace SDK::wh::environmentmodule {
    bool TimeOfDayBlenderLayout::BlendTimeOfDay(const char *preset, std::int64_t durationMs, bool force) {
        return hook::this_call<bool>(SDK::gPatterns.C_TimeOfDayBlender__BlendTimeOfDay, this, preset, durationMs, force);
    }

    void TimeOfDayBlenderLayout::ApplyPresetBlend() {
        hook::this_call<void>(SDK::gPatterns.C_TimeOfDayBlender__ApplyPresetBlend, this);
    }

    C_WeatherSystem *C_WeatherSystem::GetInstance() {
        auto *environment = C_EnvironmentModule::GetInstance();

        if (environment == nullptr) {
            return nullptr;
        }

        return static_cast<C_WeatherSystem *>(environment->GetWeatherSystem());
    }
} // namespace SDK::wh::environmentmodule
