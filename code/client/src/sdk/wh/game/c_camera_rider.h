#pragma once

#include "c_camera.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /**
     * Camera implementation used while the player is mounted.
     *
     * Construction initializes the complete tail field-by-field. Its camera
     * update path proves +0x20 as a renderer frame identifier and uses the
     * four floats at +0x24..+0x30 as two smoothed angles and their velocities.
     * The source names and remaining state semantics stay stripped.
     */
    class C_CameraRider : public C_Camera {
      public:
        ~C_CameraRider() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "FirstPerson"; }

        std::uint32_t GetState0010Raw() const { return m_state0010; }
        bool GetState0014Raw() const { return m_state0014; }
        float GetOffset0018Raw() const { return m_offset0018; }
        std::uint32_t GetLastRendererFrameId() const {
            return m_lastRendererFrameId;
        }
        float GetSmoothedAngle0024Raw() const { return m_smoothedAngle0024; }
        float GetAngleVelocity0028Raw() const { return m_angleVelocity0028; }
        float GetSmoothedAngle002CRaw() const { return m_smoothedAngle002C; }
        float GetAngleVelocity0030Raw() const { return m_angleVelocity0030; }

      private:
        std::uint32_t m_state0010;             // 0x10
        bool m_state0014;                      // 0x14
        std::byte m_padding0015[0x3];          // 0x15
        float m_offset0018;                    // 0x18
        std::byte m_padding001C[0x4];          // 0x1C
        std::uint32_t m_lastRendererFrameId;   // 0x20
        float m_smoothedAngle0024;             // 0x24
        float m_angleVelocity0028;             // 0x28
        float m_smoothedAngle002C;             // 0x2C
        float m_angleVelocity0030;             // 0x30
        std::byte m_padding0034[0x4];          // 0x34
        void *m_pState0038;                    // 0x38
        void *m_pState0040;                    // 0x40
        std::byte m_unset0048[0x1C];           // 0x48, neither C_CameraManager's ctor nor any C_CameraRider virtual touches it
        std::uint32_t m_state0064;             // 0x64
    };

    static_assert(sizeof(C_CameraRider) == 0x68,
                  "C_CameraRider size mismatch");
} // namespace SDK::wh::game
