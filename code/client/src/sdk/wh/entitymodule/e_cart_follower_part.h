#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Pivot on a cart that a follower entity is driven from.
     *
     * Named by the game's RTTR registration of wh::entitymodule::E_CartFollowerPart
     * at 0x180101050. A cart is an entity hierarchy rather than a single
     * entity: the horses hang off the horse pivot through the "slt_horse_left"
     * and "slt_horse_right" attachment slots, and a wagon's trailing half
     * hangs off the back axel pivot through "slt_back_part".
     */
    struct E_CartFollowerPart {
        enum Type : std::uint8_t {
            HorsePivot     = 0,
            FrontAxelPivot = 1,
            BackAxelPivot  = 2,

            Count = 3,
        };
    };

    static_assert(sizeof(E_CartFollowerPart::Type) == 0x1, "E_CartFollowerPart::Type size mismatch");
} // namespace SDK::wh::entitymodule
