#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Harness position a horse is hitched into.
     *
     * Named by the game's RTTR registration of wh::entitymodule::E_CartHorseSlot
     * at 0x1800FF890. The entity attachment slots the two map onto are
     * "slt_horse_left" and "slt_horse_right".
     */
    struct E_CartHorseSlot {
        enum Type : std::uint8_t {
            Left  = 0,
            Right = 1,

            Count = 2,
        };
    };

    static_assert(sizeof(E_CartHorseSlot::Type) == 0x1, "E_CartHorseSlot::Type size mismatch");
} // namespace SDK::wh::entitymodule
