#pragma once

#include "c_combat_camera.h"

#include <cstddef>

namespace SDK::wh::combatmodule {
    /**
     * Hit-reaction combat camera.
     *
     * Its complete nine-slot table is at 0x183C29F70. Construction proves a
     * scalar field at +0x30 and native 0x50-byte callback/interpolator at
     * +0x38; scalar deletion proves the complete 0x88-byte extent.
     */
    class C_CombatCameraHitReaction : public C_CombatCamera {
      public:
        ~C_CombatCameraHitReaction() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}

        float GetState0030Raw() const { return m_state0030; }

      private:
        float m_state0030;                 // 0x30
        std::byte m_padding0034[0x4];
        std::byte m_scalarState0038[0x50]; // 0x38
    };

    static_assert(sizeof(C_CombatCameraHitReaction) == 0x88,
                  "C_CombatCameraHitReaction size mismatch");
} // namespace SDK::wh::combatmodule
