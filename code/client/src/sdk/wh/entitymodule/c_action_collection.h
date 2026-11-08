#pragma once

#include "../framework/i_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The actions an actor currently has, as the engine holds them.
     *
     * `C_ActionActor` keeps it at `+0x48` and hands it out through vtable slot
     * 3, which is `return *(this + 72)` and nothing else (`0x180856CF0`).
     * Inside it is a plain range of reference-counted actions between `+0x38`
     * and `+0x40`: `0x180856D68`, the `find_if` behind every one of the
     * engine's own `C_ActionActor::FindXxxAction` helpers, walks exactly that.
     *
     * Reading it answers a question about live engine state rather than about
     * anything this module remembered, which is the difference between an
     * answer that can go stale and one that cannot.
     */
    class C_ActionCollection {
      public:
        /**
         * The action of `type` this actor is holding, or nullptr.
         *
         * Borrowed. The collection owns the reference for as long as the
         * action is in it, and this takes none of its own -- so read what is
         * needed inside the call and keep nothing.
         *
         * The same search `C_ActionActor::FindRiderAction` makes, with the
         * type asked for rather than baked in. A hit means an action of this
         * type occupies a slot, which
         * lasts from the moment it is queued until the engine takes it out
         * again.
         */
        wh::framework::I_Action *FindActionOfType(std::uint8_t type) const;

        /** Every action this actor is holding, in the engine's order. Borrowed, as above. */
        template <typename Visitor>
        void ForEachAction(Visitor &&visit) const {
            for (wh::framework::I_Action **entry = m_begin; entry != m_end; ++entry) {
                if (*entry != nullptr) {
                    visit(*entry);
                }
            }
        }

      private:
        std::byte m_opaque0000[0x38];
        wh::framework::I_Action **m_begin; // 0x038
        wh::framework::I_Action **m_end;   // 0x040

        static void AssertLayout() {
            static_assert(offsetof(C_ActionCollection, m_begin) == 0x38, "C_ActionCollection::m_begin offset mismatch");
            static_assert(offsetof(C_ActionCollection, m_end) == 0x40, "C_ActionCollection::m_end offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
