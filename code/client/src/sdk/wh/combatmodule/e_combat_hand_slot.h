#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Which hand an item, an attack or a block belongs to.
     *
     * The combat model carries two of these and `CombatHitInfo` a third, so
     * the type and its width are proven -- the model's constructor writes the
     * value slot as a DWORD, and the property is built with a static default
     * of 1 rather than 0, which means "no hand" is not the default.
     *
     * The enumerator labels are stripped: this enum is not RTTR-registered,
     * unlike the seven combat enums that are. They are deliberately not
     * guessed here. The combat debug tab prints the raw value, which is how
     * they get established.
     */
    struct E_HandSlot {
        enum Type : std::uint32_t {
            /**
             * The value the combat model's two hand properties are built
             * with. Named for what it is rather than for the label the
             * binary does not carry -- what "1" means as a hand is one of
             * the things the debug tab still has to establish.
             */
            Default = 1,
        };
    };

    static_assert(sizeof(E_HandSlot::Type) == 0x4, "E_HandSlot::Type size mismatch");
} // namespace SDK::wh::entitymodule

namespace SDK::wh::combatmodule {
    /**
     * How a fighter is blocking.
     *
     * One byte, proven by the model constructor writing the value slot as a
     * BYTE. Not RTTR-registered, so the enumerators are unknown and not
     * guessed.
     */
    struct E_BlockMode {
        enum Type : std::uint8_t {};
    };

    /**
     * Which locomotion state combat's own animation action is in.
     *
     * One byte, proven the same way. Distinct from the actor's Mannequin
     * stance and MoveSpeed groups -- this is combat's own movement state,
     * which is why it sits in the combat model rather than in the tag state.
     */
    struct E_AnimActionMovementState {
        enum Type : std::uint8_t {};
    };

    /**
     * Scope of a guard request, used as the index of the model's guard-request
     * array property.
     *
     * The array is eight wide, which bounds the enumerator count at eight.
     * The labels are stripped.
     */
    struct E_GuardRequestScope {
        enum Type : std::uint32_t {
            /** Width of the model's C_ModelArrayProperty keyed on this. */
            Count = 8,
        };
    };

    static_assert(sizeof(E_BlockMode::Type) == 0x1, "E_BlockMode::Type size mismatch");
    static_assert(sizeof(E_AnimActionMovementState::Type) == 0x1, "E_AnimActionMovementState::Type size mismatch");
} // namespace SDK::wh::combatmodule
