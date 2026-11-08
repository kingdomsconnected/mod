#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * One row of the game's buff tables, as the table database holds it.
     *
     * The layout comes from the column binder at `0x1801A8C90`, which
     * registers every field with its offset and type. The strings are the
     * database's own and live as long as the tables do.
     */
    struct S_BuffDefinition {
        CryGUID buffId;                        // 0x00, the primary key AddBuff takes
        const char *buffName;                  // 0x10
        const char *buffDesc;                  // 0x18
        std::int32_t buffClassId;              // 0x20
        std::int32_t buffFamilyId;             // 0x24, -1 for none
        const char *iconId;                    // 0x28
        float duration;                        // 0x30, negative is indefinite
        std::int32_t buffExclusivityId;        // 0x34, drives the arbiter
        const char *implementation;            // 0x38, picks the C++ class
        const char *buffParams;                // 0x40, the modifier list
        const char *buffUiName;                // 0x48
        std::int32_t buffUiTypeId;             // 0x50
        std::int32_t buffAiTagId;              // 0x54, -1 for none
        std::int32_t buffUiOrder;              // 0x58
        bool isPersistent;                     // 0x5C
        std::byte m_padding005D[3];            // 0x5D, alignment: binder 0x1801A8C90 registers no column here
        const char *visualEffect;              // 0x60
        std::int32_t slotIconId;               // 0x68
        std::byte m_padding006C[4];            // 0x6C, alignment: no column registered
        const char *slotBuffUiName;            // 0x70
        std::int32_t buffLifetimeId;           // 0x78, 0 ShortTerm, 1 LongTerm; gates LongTermValueQuery
        std::int32_t buffUiVisibilityId;       // 0x7C
        std::int32_t buffHcModeUiVisibilityId; // 0x80
        std::byte m_opaque0084[0xC];           // 0x84, table-database bookkeeping
        const char *factoryClassName;          // 0x90, suffix used by 0x18063AE40

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_BuffDefinition, buffId) == 0x00, "S_BuffDefinition::buffId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffName) == 0x10, "S_BuffDefinition::buffName offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffDesc) == 0x18, "S_BuffDefinition::buffDesc offset mismatch");
            static_assert(offsetof(S_BuffDefinition, iconId) == 0x28, "S_BuffDefinition::iconId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffParams) == 0x40, "S_BuffDefinition::buffParams offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffUiName) == 0x48, "S_BuffDefinition::buffUiName offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffUiTypeId) == 0x50, "S_BuffDefinition::buffUiTypeId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffUiOrder) == 0x58, "S_BuffDefinition::buffUiOrder offset mismatch");
            static_assert(offsetof(S_BuffDefinition, visualEffect) == 0x60, "S_BuffDefinition::visualEffect offset mismatch");
            static_assert(offsetof(S_BuffDefinition, slotIconId) == 0x68, "S_BuffDefinition::slotIconId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, slotBuffUiName) == 0x70, "S_BuffDefinition::slotBuffUiName offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffUiVisibilityId) == 0x7C, "S_BuffDefinition::buffUiVisibilityId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, factoryClassName) == 0x90, "S_BuffDefinition::factoryClassName offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffClassId) == 0x20, "S_BuffDefinition::buffClassId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, duration) == 0x30, "S_BuffDefinition::duration offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffExclusivityId) == 0x34, "S_BuffDefinition::buffExclusivityId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, implementation) == 0x38, "S_BuffDefinition::implementation offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffAiTagId) == 0x54, "S_BuffDefinition::buffAiTagId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, isPersistent) == 0x5C, "S_BuffDefinition::isPersistent offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffLifetimeId) == 0x78, "S_BuffDefinition::buffLifetimeId offset mismatch");
            static_assert(offsetof(S_BuffDefinition, buffHcModeUiVisibilityId) == 0x80, "S_BuffDefinition::buffHcModeUiVisibilityId offset mismatch");
        }
    };

    static_assert(sizeof(S_BuffDefinition) == 0x98, "S_BuffDefinition size mismatch");
} // namespace SDK::wh::rpgmodule
