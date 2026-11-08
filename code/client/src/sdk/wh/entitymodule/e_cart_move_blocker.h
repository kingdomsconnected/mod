#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Reason a cart is being held still. Each is an independent counter, not
     * a state: I_Cart::SetMoveBlocker writes one slot of the int array at
     * C_Cart+0xB8 and leaves the others alone.
     *
     * Named by the game's RTTR registration of wh::entitymodule::E_CartMoveBlocker
     * at 0x1800FF070. Lua's Cart.BlockByStash raises Stash on the cart it
     * finds behind the given entity.
     */
    struct E_CartMoveBlocker {
        enum Type : std::uint8_t {
            NoDriver = 0,
            Event    = 1,
            Obstacle = 2,
            Stash    = 3,
            Player   = 4,

            Count = 5,
        };
    };

    static_assert(sizeof(E_CartMoveBlocker::Type) == 0x1, "E_CartMoveBlocker::Type size mismatch");
} // namespace SDK::wh::entitymodule
