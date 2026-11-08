#pragma once

#include "wuid.h"

#include <cstddef>

namespace SDK::wh::framework {
    /**
     * Abstract keyed override/conflict manager.
     *
     * The WUID/WUID specialization's table at 0x183EAAE60 consists of the
     * virtual destructor and two pure operations. Its default constructor,
     * deleting destructor, and use as C_HorseUsageManager's base prove the
     * exact 0x18-byte specialization layout.
     */
    template <typename TKey, typename TValue>
    class C_OverrideConflictManager {
      public:
        virtual ~C_OverrideConflictManager() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;

      private:
        // A std::map: the ctor (0x181064EC0) allocates the 0x68-byte tree head
        // (red-black sentinel, colour/nil bytes 0x0101); the dtor frees it.
        void *m_pTreeHead; // 0x08
        std::size_t m_treeSize; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_OverrideConflictManager, m_pTreeHead) == 0x08, "C_OverrideConflictManager::m_pTreeHead offset mismatch");
            static_assert(offsetof(C_OverrideConflictManager, m_treeSize) == 0x10, "C_OverrideConflictManager::m_treeSize offset mismatch");
        }
    };

    static_assert(sizeof(C_OverrideConflictManager<WUID, WUID>) == 0x18,
                  "C_OverrideConflictManager<WUID, WUID> size mismatch");
} // namespace SDK::wh::framework
