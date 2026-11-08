#pragma once

#include "../../types.h"
#include "c_camera.h"

#include <cstddef>

namespace SDK::wh::game {
    /**
     * Standard third-person camera implementation.
     *
     * C_CameraManager constructs an identity quaternion at +0x10 and zeroes
     * the float at +0x20. The input path rotates that quaternion and adjusts
     * the trailing float, while the camera transform consumes the float as
     * its third-person distance offset.
     */
    class C_CameraThirdPerson : public C_Camera {
      public:
        ~C_CameraThirdPerson() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "ThirdPerson"; }

        const SDK::Quat &GetOrientation() const { return m_orientation; }
        float GetDistanceOffset() const { return m_distanceOffset; }

      private:
        SDK::Quat m_orientation;        // 0x10
        float m_distanceOffset;         // 0x20
        std::byte m_padding0024[0x4];   // 0x24
    };

    static_assert(sizeof(C_CameraThirdPerson) == 0x28,
                  "C_CameraThirdPerson size mismatch");
} // namespace SDK::wh::game
