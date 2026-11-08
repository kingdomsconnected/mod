#pragma once

#include <cstdint>

namespace SDK::wh::xgenaimodule::BehaviorTree {
    /**
     * Trespass severity stored by the local player's model property.
     *
     * The native type and one-byte width are constructor-proven. Its stripped
     * enumerator labels are intentionally not guessed.
     */
    struct E_TrespassLevel {
        enum Type : std::uint8_t {};
    };

    static_assert(sizeof(E_TrespassLevel::Type) == 0x1,
                  "E_TrespassLevel::Type size mismatch");
} // namespace SDK::wh::xgenaimodule::BehaviorTree
