#pragma once

#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * The manager that turns combat intent into a running action.
     *
     * Built at 0x1815647D8 into C_CombatActor+0x3A8, 0x70 bytes. It carries a
     * hash map of the actions currently running on this fighter and, inline,
     * the four factories that create them.
     *
     * Combat actions do **not** travel through `C_ActionActor` at
     * `C_Actor+0x280`. That queue carries the `I_ActorAction` family -- the
     * rider action among them. This manager carries `I_CombatActorAction`,
     * and the two families only meet at `wh::framework::I_Action`. Anything
     * modelled on the rider-action path has to be re-derived here.
     *
     * Its own table is six slots inherited from C_CombatActorObject, and so is
     * every factory's, so the whole request surface is non-virtual:
     * `C_CombatActionAttackFactory::CreateAttackAction` (0x180939310) against
     * the attack factory, and `CreateGuardAction` (0x181699500) against the
     * manager itself. Both hand back a handle carrying one reference, which
     * `C_CombatActor::RunAction` AddRefs for the queue -- so the caller still
     * has to Release its own.
     */
    class C_CombatActorActionManager: public C_CombatActorObject {
      public:
        ~C_CombatActorActionManager() override = default;

        /** How many actions this fighter is running right now. */
        std::size_t GetActiveActionCount() const {
            return m_actionCount;
        }

        bool HasActiveAction() const {
            return m_actionCount != 0;
        }

        float GetTimeScale() const {
            return m_timeScale;
        }

        /**
         * The factory that builds an attack, ready to be called.
         *
         * Each slot holds the factory object itself -- sixteen bytes, its own
         * table and the C_CombatActor it belongs to -- rather than a handle to
         * one, so this is what `C_CombatActionAttackFactory::CreateAttackAction`
         * takes as `this`. Null until the manager's constructor has run.
         */
        void *GetAttackFactory() const {
            return m_attackFactory;
        }

        void *GetGuardFactory() const {
            return m_guardFactory;
        }

        void *GetPerfectBlockFactory() const {
            return m_perfectBlockFactory;
        }

        void *GetDodgeFactory() const {
            return m_dodgeFactory;
        }

      private:
        float m_timeScale;            // 0x10, constructed as 1.0f
        std::byte m_padding0014[0x4]; // 0x14

        // std::unordered_map of the live actions. Only its element count is
        // read here: the node type is the action handle, and taking one out by
        // hand would mean owning a reference the manager also owns.
        void *m_pListHead;            // 0x18
        std::size_t m_actionCount;    // 0x20
        std::byte m_opaque0028[0x28]; // 0x28

        // The four factories, inline, each proven by the table its own
        // constructor installs.
        void *m_attackFactory;       // 0x50, C_CombatActionAttackFactory
        void *m_guardFactory;        // 0x58, C_CombatActionGuardFactory
        void *m_perfectBlockFactory; // 0x60, C_CombatActionPerfectBlockFactory
        void *m_dodgeFactory;        // 0x68, C_CombatActionDodgeFactory

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionManager, m_timeScale) == 0x10, "C_CombatActorActionManager::m_timeScale offset mismatch");
            static_assert(offsetof(C_CombatActorActionManager, m_actionCount) == 0x20, "C_CombatActorActionManager::m_actionCount offset mismatch");
            static_assert(offsetof(C_CombatActorActionManager, m_attackFactory) == 0x50, "C_CombatActorActionManager::m_attackFactory offset mismatch");
            static_assert(offsetof(C_CombatActorActionManager, m_dodgeFactory) == 0x68, "C_CombatActorActionManager::m_dodgeFactory offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionManager) == 0x70, "C_CombatActorActionManager size mismatch");
} // namespace SDK::wh::combatmodule
