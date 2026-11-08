#pragma once

#include "../databasemodule/c_object_database.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace SDK::wh::combatmodule {
    /**
     * One of the generated `CombatDb` tables: a static C_ObjectTableDatabase
     * whose rows live in the vector at +0x30/+0x38, `sizeof(TRow)` apart.
     *
     * A row is immutable once loaded and sits at the same index on every
     * machine running the same data, which is what lets a row cross the wire
     * as its index -- the attack table does exactly this already.
     */
    template <typename TRow>
    class C_CombatActionTableDatabase: public databasemodule::C_ObjectDatabase<TRow, std::vector> {
      public:
        static constexpr std::uint32_t kInvalidRowIndex = std::numeric_limits<std::uint32_t>::max();

        /** The index of `row` in this table, or kInvalidRowIndex if it is not one of its rows. */
        std::uint32_t GetRowIndex(const TRow *row) const {
            const TRow *begin = this->ObjectsBegin();
            const TRow *end   = this->ObjectsEnd();
            if (row == nullptr || begin == nullptr || end == nullptr || row < begin || row >= end) {
                return kInvalidRowIndex;
            }
            const auto delta = reinterpret_cast<std::uintptr_t>(row) - reinterpret_cast<std::uintptr_t>(begin);
            if (delta % sizeof(TRow) != 0 || delta / sizeof(TRow) >= kInvalidRowIndex) {
                return kInvalidRowIndex;
            }
            return static_cast<std::uint32_t>(delta / sizeof(TRow));
        }

        /** The row at `index`, or null past the end of the table. */
        const TRow *GetRow(std::uint32_t index) const {
            return index < this->ObjectCount() ? this->ObjectsBegin() + index : nullptr;
        }
    };
} // namespace SDK::wh::combatmodule
