#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Gait a cart is asked to travel at. Named by the game's RTTR registration
     * of wh::entitymodule::E_CartSpeed at 0x1800FE8D0; the AI side re-exports
     * the same enum as a behaviour-tree node attribute on C_CartMove.
     *
     * The speeds themselves are the wh_cart_MovementSpeedNormal and
     * wh_cart_MovementSpeedMin CVars.
     */
    struct E_CartSpeed {
        enum Type : std::uint8_t {
            Normal = 0,
            Fast   = 1,

            Count = 2,
        };
    };

    static_assert(sizeof(E_CartSpeed::Type) == 0x1, "E_CartSpeed::Type size mismatch");
} // namespace SDK::wh::entitymodule
