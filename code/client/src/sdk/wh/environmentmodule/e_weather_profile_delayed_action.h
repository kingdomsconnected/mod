#pragma once

#include <cstdint>

namespace SDK::wh::environmentmodule {
    /** Reflected action applied after a weather-profile transition delay. */
    struct E_WeatherProfileDelayedAction {
        enum Type : std::uint8_t {
            Hide = 0,
            Unhide = 1,
            DisableEmitter = 2,
        };
    };

    static_assert(sizeof(E_WeatherProfileDelayedAction::Type) == 0x1,
                  "E_WeatherProfileDelayedAction::Type size mismatch");
} // namespace SDK::wh::environmentmodule
