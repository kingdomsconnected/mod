#pragma once

#include "../../cryengine/entitysystem/i_entity.h"

#include <cstddef>

namespace SDK::wh::combatmodule {
    /**
     * Who a fighter has locked on to, held at `C_CombatActorModel +0xCF0`.
     *
     * `C_CombatActorOpponentManager::SetTarget` (0x180B21384) builds one on
     * the heap -- its table at 0x183A796F0 and the entity at +0x08 -- and
     * that is the whole of the lock. It is not yet the opponent: the manager
     * promotes the target to `+0xCF8` itself, now if
     * `CanSwitchOpponentNow` (0x1809D32E4) allows and otherwise on the first
     * update that finds the fighter at rest (`SyncOpponentToTarget`,
     * 0x180A518F4). Null when nothing is targeted.
     */
    class C_CombatTarget {
      public:
        virtual ~C_CombatTarget() = default; // slot 0

        /** The targeted entity. Slot 1 (0x180947210) returns +0x08. */
        virtual IEntity *GetEntity() const = 0;

      private:
        IEntity *m_pEntity; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_CombatTarget, m_pEntity) == 0x08, "C_CombatTarget::m_pEntity offset mismatch");
        }
    };

    // SetTarget copies a 16-byte stack instance of exactly this into the heap.
    static_assert(sizeof(C_CombatTarget) == 0x10, "C_CombatTarget size mismatch");
} // namespace SDK::wh::combatmodule
