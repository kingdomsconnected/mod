#pragma once

#include "../../cryengine/action/i_action_map_event_listener.h"
#include "../../types.h"
#include "../framework/i_source_monitor_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player controller for ranged/projectile weapons.
     *
     * RTTI proves IActionMapEventListener at +0x00 and
     * I_SourceMonitorListener at +0x08. Its primary table at 0x183A80E30 and
     * secondary table at 0x183A80E20 implement exactly the two base contracts
     * and add no operations. C_Player's constructor embeds this controller at
     * C_Player+0xD70, passes the enclosing player to the controller constructor,
     * and that constructor stores it at +0x18. Sized deletion proves the
     * 0x40-byte layout.
     */
    class C_MissileWeaponPlayerController
        : public SDK::IActionMapEventListener
        , public wh::framework::I_SourceMonitorListener {
      public:
        ~C_MissileWeaponPlayerController() override = default;
        void unk_fn0001() override;
        void unk_fn0000() override;
        C_Player *GetPlayer() const {
            return m_pPlayer;
        }

      private:
        std::int64_t m_time0010;            // 0x10, ctor 0x180BC6D5C seeds -100000 (a "never" timestamp)
        C_Player *m_pPlayer;                // 0x18
        std::uint32_t m_state0020;          // 0x20, ctor zeroes
        std::uint16_t m_state0024;          // 0x24, ctor zeroes
        std::uint8_t m_state0026;           // 0x26, ctor zeroes
        std::byte m_padding0027[0x1];       // 0x27
        SDK::CryStringT<char> m_name0028;   // 0x28, ctor sets the shared empty string
        std::uint32_t m_value0030;          // 0x30, ctor sets 20
        std::uint32_t m_value0034;          // 0x34, ctor sets 0xFF000000
        std::uint8_t m_flag0038;            // 0x38, ctor sets 1
        std::uint8_t m_flag0039;            // 0x39, ctor sets 1
        std::byte m_padding003A[0x6];       // 0x3A
        static void AssertLayout() {
            static_assert(offsetof(C_MissileWeaponPlayerController, m_time0010) == 0x10, "C_MissileWeaponPlayerController::m_time0010 offset mismatch");
            static_assert(offsetof(C_MissileWeaponPlayerController, m_name0028) == 0x28, "C_MissileWeaponPlayerController::m_name0028 offset mismatch");
            static_assert(offsetof(C_MissileWeaponPlayerController, m_value0030) == 0x30, "C_MissileWeaponPlayerController::m_value0030 offset mismatch");
            static_assert(offsetof(C_MissileWeaponPlayerController, m_flag0038) == 0x38, "C_MissileWeaponPlayerController::m_flag0038 offset mismatch");
            static_assert(offsetof(C_MissileWeaponPlayerController, m_pPlayer) == 0x18, "Missile controller owner offset mismatch");
        }
    };

    static_assert(sizeof(C_MissileWeaponPlayerController) == 0x40, "C_MissileWeaponPlayerController size mismatch");
} // namespace SDK::wh::entitymodule
