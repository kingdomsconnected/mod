#pragma once

#include "../entitymodule/i_equipment_manager_listener.h"
#include "c_combat_actor.h"
#include "c_combat_camera_manager.h"
#include "c_combat_player_controller.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Native player fighter; constructor 0x180BC6E90 allocates 0x470. */
    class C_CombatPlayer
        : public C_CombatActor
        , public wh::entitymodule::I_EquipmentManagerListener {
      public:
        C_CombatPlayerController *GetController() const {
            return m_pController;
        }

        /** This fighter's own combat camera manager, built by `Init` (0x180B3E88F). */
        C_CombatCameraManager *GetCameraManager() const {
            return m_pCameraManager;
        }

      private:
        C_CombatPlayerController *m_pController;   // 0x450
        C_CombatCameraManager *m_pCameraManager;   // 0x458
        std::uint32_t m_state0460; // 0x460; CreateCombatPlayer writes 0x1000000
        std::uint16_t m_state0464; // 0x464; ctor 0
        std::byte m_padding0466[0x2];
        std::uint32_t m_state0468; // 0x468; ctor 0
        std::byte m_padding046C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_CombatPlayer, m_pController) == 0x450, "C_CombatPlayer::m_pController offset mismatch");
            static_assert(offsetof(C_CombatPlayer, m_pCameraManager) == 0x458, "C_CombatPlayer::m_pCameraManager offset mismatch");
            static_assert(offsetof(C_CombatPlayer, m_state0460) == 0x460, "C_CombatPlayer::m_state0460 offset mismatch");
            static_assert(offsetof(C_CombatPlayer, m_state0468) == 0x468, "C_CombatPlayer::m_state0468 offset mismatch");
        }
    };
    static_assert(sizeof(C_CombatPlayer) == 0x470, "C_CombatPlayer size mismatch");
} // namespace SDK::wh::combatmodule
