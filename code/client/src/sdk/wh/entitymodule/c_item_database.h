#pragma once

#include "../databasemodule/c_object_tree_database.h"
#include "s_item_class.h"
#include "s_item_class_guid.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace SDK::wh::entitymodule {
    /**
     * Every item class the game knows, one process-global object
     * (0x185325820).
     *
     * RTTI proves the base: `C_ObjectTreeDatabase<std::shared_ptr<S_ItemClass>,
     * std::vector>`. The constructor (0x1815416BC) initialises this class's
     * own state out to +0xE0, the key index `FindItemClass` searches among it
     * at +0x78.
     */
    class C_ItemDatabase: public databasemodule::C_ObjectTreeDatabase<std::shared_ptr<S_ItemClass>, std::vector> {
      public:
        ~C_ItemDatabase() override = default;

        static C_ItemDatabase *GetInstance();

        /** The class `itemClass` names, or null for one this build does not have. */
        const S_ItemClass *FindItemClass(const S_ItemClassGUID &itemClass) const;

        /**
         * The same row, writable. The database owns its rows through
         * `std::shared_ptr` and hands out const views; the server-ordered stat
         * overrides rewrite the authored numbers in place, which is what a
         * table patch does at load and what every reader -- the hit
         * calculation, the inventory screen, the shops -- then sees on its
         * next look. Nothing else writes through this.
         */
        S_ItemClass *FindMutableItemClass(const S_ItemClassGUID &itemClass) {
            return const_cast<S_ItemClass *>(FindItemClass(itemClass));
        }

      private:
        // The singleton's initializer 0x1815416BC writes the members below into the global at 0x185325820.
        void *m_value0078;          // 0x78, zeroed; the key index FindItemClass searches starts here
        std::byte m_state0080[0x20]; // 0x80, zeroed
        void *m_value00A0;          // 0xA0, zeroed
        std::byte m_map00A8[0x10];  // 0xA8, native std::map: head node (0x28 bytes, self-linked, isnil) then size
        std::byte m_state00B8[0x10]; // 0xB8, copied from the 16-byte constant at 0x183A38B00
        void *m_value00C8;          // 0xC8, zeroed
        std::byte m_state00D0[0x10]; // 0xD0, zeroed

        static void AssertLayout() {
            static_assert(offsetof(C_ItemDatabase, m_value0078) == 0x78, "C_ItemDatabase::m_value0078 offset mismatch");
            static_assert(offsetof(C_ItemDatabase, m_map00A8) == 0xA8, "C_ItemDatabase::m_map00A8 offset mismatch");
            static_assert(offsetof(C_ItemDatabase, m_state00D0) == 0xD0, "C_ItemDatabase::m_state00D0 offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemDatabase) == 0xE0, "C_ItemDatabase size mismatch");
} // namespace SDK::wh::entitymodule
