#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Combat state published by the local player's model property.
     *
     * The native type and one-byte width are constructor-proven. Its stripped
     * enumerator labels are intentionally not guessed.
     */
    struct E_CombatState {
        enum Type : std::uint8_t {};
    };

    static_assert(sizeof(E_CombatState::Type) == 0x1,
                  "E_CombatState::Type size mismatch");
} // namespace SDK::wh::entitymodule
