#pragma once

#include "c_combat_camera.h"

namespace SDK::wh::combatmodule {
    /**
     * Animation-driven combat camera with no state beyond C_CombatCamera.
     *
     * The complete nine-slot table at 0x183C3EBD8 and constructor at
     * 0x18193D234 prove the concrete identity and unchanged 0x30-byte extent.
     */
    class C_CombatCameraAnimate : public C_CombatCamera {
      public:
        ~C_CombatCameraAnimate() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}
    };

    static_assert(sizeof(C_CombatCameraAnimate) == 0x30,
                  "C_CombatCameraAnimate size mismatch");
} // namespace SDK::wh::combatmodule
