#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What a preparation makes of an item. Named by the game's registration
     * of wh::entitymodule::E_PrepareFood::Type at 0x18026B9B0; the behaviour
     * trees spell it as the PrepareFood node's `PrepareType`.
     *
     * C_PrepareFoodEffect::GetPreparedItemClassId (0x182CADD70) picks the
     * food's CookedItemId, SmokedItemId or DriedItemId by it, and herb drying
     * (C_RPGInventory::DryHerbs, 0x182CF2420) passes Dry.
     */
    struct E_PrepareFood {
        enum Type : std::uint8_t {
            Cook  = 0,
            Smoke = 1,
            Dry   = 2,
        };
    };

    static_assert(sizeof(E_PrepareFood::Type) == 0x1, "E_PrepareFood::Type size mismatch");
} // namespace SDK::wh::entitymodule
