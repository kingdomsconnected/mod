#pragma once

#include "e_ui_state.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The service that suppresses pieces of the HUD, at
     * C_GlobalEnvironment +0xD8. Both Block slots are proven off
     * C_BlockUINotifications::OnActivated (0x182B13554) and its OnDeactivated
     * twin.
     *
     * The live object's RTTI is `wh::framework::C_SourceMonitor` (vtable
     * 0x183E1A500), a generic monitor instantiated over this enum.
     *
     * It does no reference counting, so the pairing is the caller's: blocking
     * twice and unblocking once leaves the element showing.
     *
     * Only the prefix up to the held bitset is laid out: the object is only
     * ever reached through the pointer, and its size has not been proven.
     */
    class C_UIStateBlockers {
      public:
        /** Null before the GUI module is built. */
        static C_UIStateBlockers *GetInstance();

        virtual void Block(E_UIState state)   = 0; // slot 0, 0x1808BD1F8
        virtual void Unblock(E_UIState state) = 0; // slot 1, 0x1808BD184

        /** Tests the held set against an {any, none} mask pair. The exact truth table is not proven. */
        virtual bool TestMasks(const std::uint64_t *masks) const = 0; // slot 2, 0x1804AB57C

        /**
         * Not "is this state held": `rule` indexes a table of mask pairs at
         * +0x10 and forwards the pair to TestMasks. The photo-mode gate at
         * 0x182135210 passes rule 17. Use IsHeld for a state.
         */
        virtual bool TestRule(std::uint8_t rule) const = 0; // slot 3, 0x1804AB554

        /** Whether a state is currently blocked -- the bit Block sets and Unblock clears. */
        bool IsHeld(E_UIState state) const {
            return (m_heldStates & (std::uint64_t {1} << static_cast<std::uint8_t>(state))) != 0;
        }

        /** Every held state at once, one bit per `E_UIState`. */
        std::uint64_t GetHeldStates() const {
            return m_heldStates;
        }

      protected:
        // Slot 0 is Block, so there is no destructor to dispatch to.
        ~C_UIStateBlockers() = default;

      private:
        // A std::bitset<51>: Block and Unblock bounds-check against 0x33
        // (0x181E26100, 0x181E260A0) and address it as one qword.
        std::uint64_t m_heldStates; // 0x8

        static void AssertLayout() {
            static_assert(offsetof(C_UIStateBlockers, m_heldStates) == 0x8, "C_UIStateBlockers::m_heldStates offset mismatch");
        }
    };
} // namespace SDK::wh::guimodule
