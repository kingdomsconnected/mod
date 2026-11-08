#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Seat on a cart. Every occupancy, transform and permission query on
     * I_Cart is indexed by this.
     *
     * Labels and values come from the game's own RTTR registration of
     * wh::entitymodule::E_CartPosition at 0x180100030, which names the seven
     * enumerators in declaration order. The one-byte width is proven by the
     * call sites: I_Cart's seat operations take the position in a byte
     * register, and C_Cart's parallel arrays are indexed by it directly.
     *
     * Driver and codriver are the bench up front; teamster is the walker
     * beside the horses, which I_Cart::GetPositionTransform special-cases.
     * Codriver and rightBack are the two seats the player is allowed into --
     * see I_Cart::CanOccupyPosition, which inverts its player test for every
     * other position.
     */
    struct E_CartPosition {
        enum Type : std::uint8_t {
            Driver     = 0,
            Codriver   = 1,
            Teamster   = 2,
            LeftFront  = 3,
            RightFront = 4,
            LeftBack   = 5,
            RightBack  = 6,

            Count = 7,
        };
    };

    static_assert(sizeof(E_CartPosition::Type) == 0x1, "E_CartPosition::Type size mismatch");
} // namespace SDK::wh::entitymodule
