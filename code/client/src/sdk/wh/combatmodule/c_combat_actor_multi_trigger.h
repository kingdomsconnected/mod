#pragma once

#include "../../patterns.h"
#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * One kind of defensive window a fighter can be offered, holding one
     * child trigger per attacker that opened it.
     *
     * Each fighter's `C_CombatActorDirector` keeps one per kind, found by
     * `C_CombatActorDirector::FindSlotObject` (0x180B277A0). An attacker's
     * slot planner opens a window on its opponent by handing the opponent's
     * object to `AddTriggerForOpponent` (0x180914204), which pushes a
     * `{attacker, trigger}` pair onto the vector at `+0x20` and starts the
     * window. 0x38 bytes, as the creators allocate it (the block one is
     * 0x180FC0F5C).
     *
     * The kinds are subclasses with nothing but their own table and the type
     * id their creator writes at `+0x18`: block, dodge, master strike, perfect
     * block, sync perfect block. Only the block is told apart here.
     */
    class C_CombatActorMultiTrigger {
      public:
        /**
         * Whether this is the block window (`C_CombatMultiTriggerBlock`).
         *
         * Its trigger raises the defender's guard again as the attacker
         * strikes, while the defender's guard request is up -- the native
         * way a held block meets every blow
         * (`C_CombatTriggerBlock::OnOwnerCombatStateChanged`, 0x1809089AC).
         * It decides nothing about the defence; every other kind does.
         */
        bool IsBlock() const {
            return reinterpret_cast<std::uint64_t>(m_pVTable) == gPatterns.C_CombatMultiTriggerBlock__VTable;
        }

      private:
        const void *m_pVTable;         // 0x00
        // The creators construct a C_CombatActorUpdatedObject of update kind
        // 0 first (0x180914754), so these two are that base's members; the
        // class stays non-polymorphic here so its table can be compared.
        C_CombatActor *m_pCombatActor; // 0x08, the fighter whose director holds it
        std::uint32_t m_updateKind;    // 0x10, 0: never registered for updates
        std::byte m_padding0014[0x4];  // 0x14
        std::uint32_t m_typeId;        // 0x18, 1 for the block
        std::byte m_padding001C[0x4];  // 0x1C
        std::byte m_triggers[0x18];    // 0x20, std::vector of {attacker, trigger}

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorMultiTrigger, m_pCombatActor) == 0x08, "C_CombatActorMultiTrigger::m_pCombatActor offset mismatch");
            static_assert(offsetof(C_CombatActorMultiTrigger, m_updateKind) == 0x10, "C_CombatActorMultiTrigger::m_updateKind offset mismatch");
            static_assert(offsetof(C_CombatActorMultiTrigger, m_typeId) == 0x18, "C_CombatActorMultiTrigger::m_typeId offset mismatch");
            static_assert(offsetof(C_CombatActorMultiTrigger, m_triggers) == 0x20, "C_CombatActorMultiTrigger::m_triggers offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorMultiTrigger) == 0x38, "C_CombatActorMultiTrigger size mismatch");
} // namespace SDK::wh::combatmodule
