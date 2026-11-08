#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Outcome a cart reports when it finishes travelling to its target.
     * Named by the game's RTTR registration of wh::entitymodule::E_CartChange
     * at 0x1801008B0.
     */
    struct E_CartChange {
        enum Type : std::uint8_t {
            TargetReached    = 0,
            TargetNotReached = 1,

            Count = 2,
        };
    };

    static_assert(sizeof(E_CartChange::Type) == 0x1, "E_CartChange::Type size mismatch");
} // namespace SDK::wh::entitymodule
