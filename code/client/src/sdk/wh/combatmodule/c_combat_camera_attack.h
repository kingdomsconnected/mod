#pragma once

#include "c_combat_camera.h"

#include <cstddef>

namespace SDK::wh::combatmodule {
    /**
     * Attack combat camera.
     *
     * Its complete nine-slot table is at 0x183C299F8. Construction proves a
     * native 0x50-byte scalar callback/interpolator at +0x30 and a trailing
     * float at +0x80; scalar deletion proves the complete 0x88-byte extent.
     */
    class C_CombatCameraAttack : public C_CombatCamera {
      public:
        ~C_CombatCameraAttack() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}

        float GetState0080Raw() const { return m_state0080; }

      private:
        std::byte m_scalarState0030[0x50]; // 0x30
        float m_state0080;                 // 0x80
        std::byte m_padding0084[0x4];
    };

    static_assert(sizeof(C_CombatCameraAttack) == 0x88,
                  "C_CombatCameraAttack size mismatch");
} // namespace SDK::wh::combatmodule
