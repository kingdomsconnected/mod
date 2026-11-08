#pragma once

#include "../../types.h"
#include "c_camera.h"

namespace SDK::wh::game {
    /**
     * Camera implementation used while native UI owns the view.
     *
     * Its table at 0x183A5A280 implements exactly C_Camera's 13 entries. The
     * constructor caches the system camera FOV in radians at +0x10. Its view
     * update copies the quaternion at +0x14 and position at +0x24 into the
     * matching SViewParams fields; the reciprocal override captures those
     * same fields from another camera. The scalar deleting destructor proves
     * the complete 0x30-byte extent.
     */
    class C_CameraUI : public C_Camera {
      public:
        ~C_CameraUI() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "UI"; }

        float GetFieldOfViewRadians() const { return m_fieldOfViewRadians; }
        const SDK::Quat &GetRotation() const { return m_rotation; }
        const SDK::Vec3 &GetPosition() const { return m_position; }

      private:
        float m_fieldOfViewRadians; // 0x10
        SDK::Quat m_rotation;        // 0x14
        SDK::Vec3 m_position;        // 0x24
    };

    static_assert(sizeof(C_CameraUI) == 0x30,
                  "C_CameraUI size mismatch");
} // namespace SDK::wh::game
