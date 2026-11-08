#pragma once

#include "../../types.h"
#include "c_entity_module.h"
#include "c_save_extension_manager.h"
#include "c_stash.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One node of the manager's `CryGUID -> C_Stash *` container: the MSVC list
     * links, then the pair. The engine's lookup reads the value at +0x20 and
     * its insert writes it there; the head sentinel is 0x28 bytes.
     */
    struct StashEntryNodeLayout {
        void *m_pNext;     // 0x00
        void *m_pPrev;     // 0x08
        CryGUID m_key;     // 0x10
        C_Stash *m_pStash; // 0x20
    };

    static_assert(sizeof(StashEntryNodeLayout) == 0x28, "StashEntryNodeLayout size mismatch");

    /**
     * The process-wide registry of every live stash, keyed by entity GUID.
     *
     * A `C_SaveExtensionManager<C_Stash>` reached as `*(entityModule + 0xF0)`,
     * so the primary hash at +0x18 is the index `Find` searches and that
     * `C_Stash::Init` consults before accepting a new stash.
     *
     * Three of the base's five stripped virtuals are proven here: slot 1
     * Add(C_Stash *), slot 2 Remove(C_Stash *) -- what `C_Stash::Release`
     * calls -- and slot 3, which reaches `C_Stash::RestockNow`.
     *
     * Two allocation sites request 0xA8 bytes, which proves the size.
     */
    class C_StashManager: public C_SaveExtensionManager<C_Stash> {
      public:
        ~C_StashManager() override = default;

        void unk_fn0001(C_Stash *stash) override;
        void unk_fn0002(C_Stash *stash) override;
        void unk_fn0003(void *state) override;
        void unk_fn0004() override;
        void unk_fn0005(C_Stash *stash) override;

        static C_StashManager *GetInstance() {
            return C_EntityModule::GetInstance()->GetStashManager();
        }

        /**
         * The registered stash with that GUID, or null -- exactly the condition
         * that makes `C_Stash::Init` fail, so also the check to run before
         * reusing a GUID.
         *
         * Walked rather than called: the engine's `Find` at 0x180A2E6D8 is
         * byte-identical to an unrelated template instantiation at 0x182A69198
         * in everything but its wildcarded call displacements, so no
         * entry-point signature can separate them. Linear rather than hashed,
         * which is fine for a spawn-time call.
         */
        C_Stash *Find(const CryGUID &stashGuid) const {
            const auto *head = static_cast<const StashEntryNodeLayout *>(GetPrimaryEntries().m_pListHead);
            for (const auto *node = static_cast<const StashEntryNodeLayout *>(head->m_pNext); node != head; node = static_cast<const StashEntryNodeLayout *>(node->m_pNext)) {
                if (node->m_key.hipart == stashGuid.hipart && node->m_key.lopart == stashGuid.lopart) {
                    return node->m_pStash;
                }
            }
            return nullptr;
        }

        C_Stash *FindByEntityGuid(EntityGUID entityGuid) const {
            const CryGUID key {0, entityGuid};
            return Find(key);
        }

      private:
        // An MSVC red-black tree, head node 64 bytes with the usual three
        // self-links and a {_Color, _Isnil} pair. What it orders has not been
        // established, so only its storage is named.
        void *m_pOrderedHead;    // 0x98
        std::uint64_t m_orderedCount; // 0xA0

        static void AssertLayout() {
            static_assert(offsetof(C_StashManager, m_pOrderedHead) == 0x98, "C_StashManager::m_pOrderedHead offset mismatch");
            static_assert(offsetof(C_StashManager, m_orderedCount) == 0xA0, "C_StashManager::m_orderedCount offset mismatch");
        }
    };

    static_assert(sizeof(C_StashManager) == 0xA8, "C_StashManager size mismatch");
} // namespace SDK::wh::entitymodule
