#pragma once

#include "../../types.h"
#include "s_buff_definition.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class C_Soul;

    /**
     * One live buff on a soul, read only.
     *
     * Never constructed here: the game's factory builds the concrete subclass
     * named by the definition's `implementation` column, one of 190. This is
     * the shape they share. Deliberately not virtual -- the native table has
     * no destructor slot in front of its methods.
     *
     * There is no "seconds remaining" accessor, and that is not an omission.
     * `C_TimedSoulBuffInstance` keeps a duration and elapsed as doubles past
     * the owning soul, but several implementations put a float in the same
     * bytes and nothing on the instance says which layout it has -- and where
     * it is a timer, six of them count game seconds rather than real ones.
     * `Shared::Game::BuffEntry::duration` answers that question instead, in
     * one unit.
     */
    class C_BuffInstanceBase {
      public:
        /** What the manager's map is keyed on, and what `RemoveBuff` takes. */
        std::uint64_t GetHandle() const {
            return m_handle;
        }

        /**
         * The row this instance is, after the per-soul remap.
         *
         * The engine treats it as the instance's identity: the manager
         * answers it for a live handle and the exclusivity collectors compare
         * candidates on it. Never null on a live instance.
         */
        const S_BuffDefinition *GetDefinition() const {
            return m_definition;
        }

        /** The soul the instance is applied to. */
        C_Soul *GetOwner() const {
            return m_owner;
        }

        /** The perk that granted this instance, or null for anything else. */
        const void *GetSourcePerk() const {
            return m_sourcePerk;
        }

      private:
        void *m_vtable;                                // 0x00
        std::uint64_t m_handle;                        // 0x08
        const S_BuffDefinition *m_definition;          // 0x10, the effective (post-remap) row
        const S_BuffDefinition *m_requestedDefinition; // 0x18, null unless the remap changed it
        void *m_sourcePerk;                            // 0x20
        std::byte m_object0028[0x10];                  // 0x28; ctor 0x18046C548 zeroes it, dtor 0x18063A690 releases it through sub_18063A740
        void *m_entries0038;                           // 0x38; vector of 16-byte elements (dtor frees with 16-byte stride); element type unproven
        void *m_entries0038End;                        // 0x40
        void *m_entries0038Capacity;                   // 0x48
        // 0x50: StackVector<S_BuffDefinitionAITag, 2>. The ctor reserves 2 into the inline buffer; the dtor checks first (0x70) against the arena (0x68)
        // before freeing, so the vector itself is first 0x70 / last 0x78 / capacity 0x80, not 0x68 as previously recorded here.
        SDK::StackVector<std::uint32_t, 2> m_aiTags;
        C_Soul *m_owner;                               // 0x88

        static void AssertLayout() {
            static_assert(offsetof(C_BuffInstanceBase, m_handle) == 0x08, "C_BuffInstanceBase::m_handle offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_definition) == 0x10, "C_BuffInstanceBase::m_definition offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_requestedDefinition) == 0x18, "C_BuffInstanceBase::m_requestedDefinition offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_sourcePerk) == 0x20, "C_BuffInstanceBase::m_sourcePerk offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_aiTags) == 0x50, "C_BuffInstanceBase::m_aiTags offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_aiTags) + SDK::StackVector<std::uint32_t, 2>::FirstOffset() == 0x70, "C_BuffInstanceBase AI tag vector offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_entries0038) == 0x38, "C_BuffInstanceBase::m_entries0038 offset mismatch");
            static_assert(offsetof(C_BuffInstanceBase, m_owner) == 0x88, "C_BuffInstanceBase::m_owner offset mismatch");
        }
    };

    // Pins this declaration, not the native allocation: every concrete
    // implementation is larger, and what follows the owning soul differs.
    static_assert(sizeof(C_BuffInstanceBase) == 0x90, "C_BuffInstanceBase layout mismatch");
} // namespace SDK::wh::rpgmodule
