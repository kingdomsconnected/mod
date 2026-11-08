#pragma once

#include "../framework/c_base_module.h"
#include "../shared/c_signal.h"
#include "c_battle_manager.h"
#include "c_combat_scene.h"
#include "c_dead_body_manager.h"

namespace SDK::wh::combatmodule {
    /**
     * Module construction allocates 0x48; player factory publishes +0x28.
     * The constructor (0x180A4602C) writes the signal at +0x10, zeroes
     * +0x20..+0x40 and creates the 0x78-byte scene at +0x20. Init (vtable
     * slot 2, 0x180D243C0) creates the three owned objects at +0x30..+0x40;
     * Shutdown (slot 3, 0x182778C10) releases them.
     */
    class C_CombatModule: public wh::framework::C_BaseModule {
      public:
        C_CombatScene *GetScene() const {
            return m_pScene;
        }
        C_CombatPlayer *GetLocalPlayer() const {
            return m_pLocalPlayer;
        }
        void SetLocalPlayer(C_CombatPlayer *player) {
            m_pLocalPlayer = player;
        }

      private:
        SDK::wh::shared::C_Signal m_signal0010;   // 0x10, dispatch 0x185666A40
        C_CombatScene *m_pScene;                  // 0x20
        C_CombatPlayer *m_pLocalPlayer;           // 0x28
        C_BattleManager *m_pBattleManager;        // 0x30
        C_DeadBodyManager *m_pDeadBodyManager;    // 0x38
        // A 0x128-byte object with no RTTI, constructor 0x180D23FE0 (six
        // signals first), destroyed by 0x1827CB520; ticked in slot 4.
        void *m_pManager0040;                     // 0x40

        static void AssertLayout() {
            static_assert(offsetof(C_CombatModule, m_pScene) == 0x20, "C_CombatModule::m_pScene offset mismatch");
            static_assert(offsetof(C_CombatModule, m_pLocalPlayer) == 0x28, "C_CombatModule::m_pLocalPlayer offset mismatch");
            static_assert(offsetof(C_CombatModule, m_signal0010) == 0x10, "C_CombatModule::m_signal0010 offset mismatch");
            static_assert(offsetof(C_CombatModule, m_pBattleManager) == 0x30, "C_CombatModule::m_pBattleManager offset mismatch");
            static_assert(offsetof(C_CombatModule, m_pDeadBodyManager) == 0x38, "C_CombatModule::m_pDeadBodyManager offset mismatch");
            static_assert(offsetof(C_CombatModule, m_pManager0040) == 0x40, "C_CombatModule::m_pManager0040 offset mismatch");
        }
    };
    static_assert(sizeof(C_CombatModule) == 0x48, "C_CombatModule size mismatch");
} // namespace SDK::wh::combatmodule
