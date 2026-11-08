#pragma once

#include "c_combat_camera.h"

namespace SDK::wh::combatmodule {
    /**
     * Bone-driven combat camera.
     *
     * Its complete nine-slot table is at 0x183B09B58. Construction initializes
     * one additional owned polymorphic object at +0x30, destruction releases
     * it, and scalar deletion proves the complete 0x38-byte extent.
     */
    class C_CombatCameraBone : public C_CombatCamera {
      public:
        ~C_CombatCameraBone() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}

        void *GetState0030Raw() const { return m_pState0030; }

      private:
        void *m_pState0030; // 0x30
    };

    static_assert(sizeof(C_CombatCameraBone) == 0x38,
                  "C_CombatCameraBone size mismatch");
} // namespace SDK::wh::combatmodule
