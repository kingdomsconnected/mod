#pragma once

#include "../../cryengine/system/cry_name.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * The game's table of input action names, one interned ActionId per
     * action.
     *
     * Built once by `C_EntityActions::C_EntityActions` (0x1817387D0) into the
     * 0x638 bytes allocated for it at 0x1810646AD, and stored at
     * `C_GlobalEnvironment+0x30`. Each member is constructed in place from its
     * literal (0x1808E5AEC), and `C_PlayerInput`'s constructor (0x180799E9C)
     * reads the same members to bind one handler per action, which is what
     * names them.
     */
    class C_EntityActions final {
      public:
        C_EntityActions()  = delete;
        ~C_EntityActions() = delete;

        /**
         * `surrender`. `C_PlayerInput` binds it to 0x182AA92E0, which sends
         * the player's soul the `player:surrender` signal (0x182AA2278) on
         * any activation. No keyboard binding ships for it: the player's
         * surrender on PC is the combat chat's "I surrender!" row.
         */
        /**
         * `sprint` (+0x30) and `toggle_run` (+0x48), interned at 0x181738DA3
         * and 0x181738DD0. `C_PlayerInput` binds them to OnSprint and
         * OnToggleRun, and `C_RiderPlayerInput`, which is offered every action
         * first, to its gallop and walk handlers.
         */
        const SDK::CCryName &GetSprint() const {
            return m_sprint;
        }
        const SDK::CCryName &GetToggleRun() const {
            return m_toggleRun;
        }

        const SDK::CCryName &GetSurrender() const {
            return m_surrender;
        }

        /**
         * `xi_movex` and `xi_movey`, the movement stick's two axes. The
         * constructor interns them at 0x1817391E7 and 0x1817391FA, and
         * `C_PlayerInput`'s binds them to `OnXiMoveX` (0x180EFE1E8) and
         * `OnXiMoveY` (0x180EFE20C) at 0x18079A277 and 0x18079A2A7.
         */
        const SDK::CCryName &GetXiMoveX() const {
            return m_xiMoveX;
        }
        const SDK::CCryName &GetXiMoveY() const {
            return m_xiMoveY;
        }

      private:
        std::byte m_vftable0000[0x8];      // 0x00, vftable
        // Ctor 0x1817387D0 interns 198 action names, one CCryName per 8 bytes from +0x08.
        SDK::CCryName m_actions0008[0x5];  // 0x08, moveleft first
        SDK::CCryName m_sprint;            // 0x30
        SDK::CCryName m_actions0038[0x2];  // 0x38
        SDK::CCryName m_toggleRun;         // 0x48
        SDK::CCryName m_actions0050[0x34]; // 0x50
        SDK::CCryName m_surrender;         // 0x1F0
        SDK::CCryName m_actions01F8[0x2];  // 0x1F8, surrender_xi_activate at 0x200
        SDK::CCryName m_xiMoveX;           // 0x208
        SDK::CCryName m_xiMoveY;           // 0x210
        SDK::CCryName m_actions0218[0x84]; // 0x218, xi_rotateyaw first

        static void AssertLayout() {
            static_assert(offsetof(C_EntityActions, m_surrender) == 0x1F0, "C_EntityActions::m_surrender offset mismatch");
            static_assert(offsetof(C_EntityActions, m_actions0008) == 0x08, "C_EntityActions::m_actions0008 offset mismatch");
            static_assert(offsetof(C_EntityActions, m_sprint) == 0x30, "C_EntityActions::m_sprint offset mismatch");
            static_assert(offsetof(C_EntityActions, m_toggleRun) == 0x48, "C_EntityActions::m_toggleRun offset mismatch");
            static_assert(offsetof(C_EntityActions, m_actions01F8) == 0x1F8, "C_EntityActions::m_actions01F8 offset mismatch");
            static_assert(offsetof(C_EntityActions, m_xiMoveX) == 0x208, "C_EntityActions::m_xiMoveX offset mismatch");
            static_assert(offsetof(C_EntityActions, m_xiMoveY) == 0x210, "C_EntityActions::m_xiMoveY offset mismatch");
            static_assert(offsetof(C_EntityActions, m_actions0218) == 0x218, "C_EntityActions::m_actions0218 offset mismatch");
        }
    };

    static_assert(sizeof(C_EntityActions) == 0x638, "C_EntityActions size mismatch");
} // namespace SDK::wh::entitymodule
