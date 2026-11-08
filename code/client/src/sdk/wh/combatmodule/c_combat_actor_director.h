#pragma once

#include "../framework/c_action_director.h"
#include "../framework/i_action.h"
#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * A fighter's combat action queue, held at `C_CombatActor +0x200`.
     *
     * Built by `C_CombatActor::Init` (0xB8 bytes, constructor 0x180917E80).
     * The constructor runs the `framework::C_ActionDirector` constructor
     * (0x1809CDAD0) on +0x00 and the `C_CombatActorObject` constructor
     * (0x180914798) on +0x90, then installs both RTTI vtables, so the class
     * derives from both in that order. `C_CombatActor::RunAction`
     * (0x18090C850) inserts every combat action here through
     * `InsertActionSlot` (0x1809CC3D4), which keeps one running action per
     * slot in the director's vector at `+0x38`, indexed by the action's own
     * `GetSlot()`: 0 is the main action -- an attack, a hit reaction, a perfect
     * block, the stance guard -- and 3 the raised block. `EnsureSlot`
     * (0x1809CCBF8) grows it, so an index past its end has simply never held
     * an action.
     *
     * The queue is ticked under the fighter's combat-group lock; read it under
     * that lock.
     */
    class C_CombatActorDirector
        : public SDK::wh::framework::C_ActionDirector
        , public C_CombatActorObject {
      public:
        /** The slot the main action runs in: a swing, a hit reaction, a perfect block, the stance guard. */
        static constexpr std::int32_t kMainSlot = 0;

        /** The action running in `slot`, or null. Borrowed: the queue holds the reference. */
        SDK::wh::framework::I_Action *GetSlotAction(std::int32_t slot) const {
            return const_cast<SDK::wh::framework::I_Action *>(GetAction(slot));
        }

      private:
        // The constructor allocates a 0x30-byte sentinel node whose +0x18
        // flags are 0x0101 (black, nil) and stores it at +0xA0 with the
        // element count at +0xA8: an MSVC std::map head keyed by slot kind
        // (int) and mapping to the slot object at node +0x28, read by
        // FindSlotObject (0x180B277A0): 2 block, 4/6/8 multi-triggers.
        void *m_pSlotObjectsHead;        // 0xA0
        std::uint64_t m_slotObjectCount; // 0xA8, 0 by default
        std::uint32_t m_state00B0;       // 0xB0, 0 by default
        std::byte m_padding00B4[0x4];    // 0xB4

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorDirector, m_pSlotObjectsHead) == 0xA0, "C_CombatActorDirector::m_pSlotObjectsHead offset mismatch");
            static_assert(offsetof(C_CombatActorDirector, m_slotObjectCount) == 0xA8, "C_CombatActorDirector::m_slotObjectCount offset mismatch");
            static_assert(offsetof(C_CombatActorDirector, m_state00B0) == 0xB0, "C_CombatActorDirector::m_state00B0 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorDirector) == 0xB8, "C_CombatActorDirector size mismatch");
} // namespace SDK::wh::combatmodule
