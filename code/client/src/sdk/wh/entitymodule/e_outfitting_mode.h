#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    struct E_OutfittingMode {
        enum Type : int {
            Unequip = 0,
            Equip = 1,
            ForceEquip = 2,
        };
    };

    static_assert(sizeof(E_OutfittingMode::Type) == 0x4,
                  "E_OutfittingMode::Type size mismatch");
} // namespace SDK::wh::entitymodule
