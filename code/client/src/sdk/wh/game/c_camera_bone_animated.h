#pragma once

#include "../animationmodule/c_bone_camera_selector.h"
#include "c_camera.h"
#include "i_camera_activation.h"

#include <cstddef>
#include <utility>

namespace SDK::wh::game {
    /** Camera driven by an animated character bone. */
    class C_CameraBoneAnimated : public C_Camera,
                                 public I_CameraActivation {
      public:
        ~C_CameraBoneAnimated() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "BoneAnimated"; }

        void activation_unk_fn0000() override {}
        void activation_unk_fn0001() override {}
        void activation_unk_fn0002() override {}
        void activation_unk_fn0003() override {}
        bool activation_unk_fn0004() const override { return false; }
        void activation_unk_fn0005(std::int32_t) override {}

        /** Release before replacing the level; Initialize (0x1839BB444) will bind the next actor. */
        bool ReleaseActor() {
            auto *selector = std::exchange(m_pSelector, nullptr);
            if (selector == nullptr) {
                return false;
            }
            // Virtual deletion dispatches to the game's scalar deleting
            // destructor, so its objects never cross allocator boundaries.
            delete selector;
            return true;
        }

      private:
        SDK::wh::animationmodule::C_BoneCameraSelector *m_pSelector; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(C_CameraBoneAnimated, m_pSelector) == 0x18);
        }
    };

    static_assert(sizeof(C_CameraBoneAnimated) == 0x20,
                  "C_CameraBoneAnimated size mismatch");
} // namespace SDK::wh::game
