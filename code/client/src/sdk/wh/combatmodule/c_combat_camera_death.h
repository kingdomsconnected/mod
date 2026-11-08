#pragma once

#include "c_combat_camera.h"

namespace SDK::wh::combatmodule {
    /**
     * Death combat camera with no state beyond C_CombatCamera.
     *
     * The complete nine-slot table at 0x183C3EB88 and constructor at
     * 0x18193D1D8 prove the concrete identity and unchanged 0x30-byte extent.
     */
    class C_CombatCameraDeath : public C_CombatCamera {
      public:
        ~C_CombatCameraDeath() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}
    };

    static_assert(sizeof(C_CombatCameraDeath) == 0x30,
                  "C_CombatCameraDeath size mismatch");
} // namespace SDK::wh::combatmodule
