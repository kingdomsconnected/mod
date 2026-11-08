#pragma once

#include "c_combat_camera.h"

namespace SDK::wh::combatmodule {
    /**
     * Guard/defence combat camera with no state beyond C_CombatCamera.
     *
     * The complete nine-slot table at 0x183C3EB38 and constructor at
     * 0x18193D17C prove the concrete identity and unchanged 0x30-byte extent.
     */
    class C_CombatCameraGuard : public C_CombatCamera {
      public:
        ~C_CombatCameraGuard() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}
    };

    static_assert(sizeof(C_CombatCameraGuard) == 0x30,
                  "C_CombatCameraGuard size mismatch");
} // namespace SDK::wh::combatmodule
