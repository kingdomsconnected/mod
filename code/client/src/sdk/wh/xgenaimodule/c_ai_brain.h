#pragma once

#include "c_variable.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    class C_NPC;

    /**
     * An NPC's mind: the object `C_NPC::SetBrain` installs.
     *
     * Two concrete kinds, chosen by `BuildBrainFromTemplate` (0x180419AA8):
     * `C_AIBrainMultiSubb` (408 bytes, vtable 0x183FD7A48) and
     * `C_AIBrainSingleSubb` (264 bytes, 0x183A37B50). Both carry the brain's
     * per-frame tick at **slot 7** -- 0x1807600B4 and 0x1807F942C -- and both
     * open it with the same gate on the flags below.
     *
     * Only what the mod reads is claimed: the owner, proven by the base
     * constructor (0x180418D10, `*(brain + 8) = npc`), the flags, set to 1 by
     * that same constructor (0x180418D2E), and slot 8, which both concrete
     * kinds share (0x181A9C2B0) and which answers the `C_VariableIndex` that
     * constructor embeds at `+0xC8`.
     */
    class C_AIBrain {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;

        /** The brain's variables, as the script binds reach them. */
        virtual C_VariableIndex *GetVariables() = 0; // slot 8, +0x40

        /** The NPC this brain thinks for. */
        C_NPC *GetOwner() const { return m_pOwner; }

        /**
         * Whether this brain would tick at all: the engine's own gate, at the head
         * of both Updates (0x1807608B4). Bit 0 is enabled and set at construction;
         * bits 2 and 4 are the ones a tick sets on itself when it has nothing left
         * to do, so the engine writes them and a mod must not.
         */
        bool WouldTick() const { return (m_flags0018 & 0x1) != 0 && (m_flags0018 & 0x4) == 0 && (m_flags0018 & 0x10) == 0; }

      private:
        C_NPC *m_pOwner;          // 0x08
        void *m_state0010;        // 0x10
        std::uint8_t m_flags0018; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(C_AIBrain, m_pOwner) == 0x08, "C_AIBrain::m_pOwner offset mismatch");
            static_assert(offsetof(C_AIBrain, m_flags0018) == 0x18, "C_AIBrain::m_flags0018 offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule
