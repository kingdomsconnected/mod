#pragma once

#include <cstdint>

namespace SDK::wh::rpgmodule {
    // HasShootingStamina (0x1815F5F80) reads state 1 through soul slot 78.
    enum class E_SoulState : std::uint32_t {
        Health     = 0,
        Stamina    = 1,
        Exhaust    = 2,
        Hunger     = 3,
        Karma      = 4,
        Alcoholism = 5,
    };
} // namespace SDK::wh::rpgmodule
