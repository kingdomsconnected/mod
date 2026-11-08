#pragma once

#include "../game/i_game_side_effect_callback.h"
#include "c_combat_player.h"

namespace SDK::wh::combatmodule {
    /** Scene constructor allocates 0x18; player Init writes the LOD anchor. */
    class C_CombatLodManager: public wh::game::I_GameSideEffectCallback {
      public:
        C_CombatPlayer *GetLocalPlayer() const {
            return m_pLocalPlayer;
        }
        void SetLocalPlayer(C_CombatPlayer *player) {
            m_pLocalPlayer = player;
        }

      private:
        bool m_sideEffectsMuted; // 0x08
        std::byte m_padding0009[0x7]; // C_CombatScene ctor 0x180A46140 writes only +0x08 and +0x10
        C_CombatPlayer *m_pLocalPlayer; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_CombatLodManager, m_sideEffectsMuted) == 0x8, "C_CombatLodManager::m_sideEffectsMuted offset mismatch");
            static_assert(offsetof(C_CombatLodManager, m_pLocalPlayer) == 0x10, "C_CombatLodManager::m_pLocalPlayer offset mismatch");
        }
    };
    static_assert(sizeof(C_CombatLodManager) == 0x18, "C_CombatLodManager size mismatch");
} // namespace SDK::wh::combatmodule
