#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Movement state of a cart, returned by I_Cart::GetState.
     *
     * Named by the game's RTTR registration of wh::entitymodule::E_CartState
     * at 0x1800FE090. The value is not stored on C_Cart itself: it lives as a
     * byte at +0x08 of the move context C_Cart holds at +0xA0, and GetState
     * reports Idle when there is no move context at all.
     *
     * C_Cart::IsMoving is Moving, or Stopping while the cart still has speed.
     */
    struct E_CartState {
        enum Type : std::uint8_t {
            Idle              = 0,
            Blocked           = 1,
            WaitingForCaravan = 2,
            NeedConfirmation  = 3,
            Moving            = 4,
            Stopping          = 5,

            Count = 6,
        };
    };

    static_assert(sizeof(E_CartState::Type) == 0x1, "E_CartState::Type size mismatch");
} // namespace SDK::wh::entitymodule
