#pragma once

#include "c_camera.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /**
     * Standard first-person camera implementation.
     *
     * C_CameraManager construction clears the 32-bit value at +0x10 and the
     * byte at +0x14. When the byte is set, the transform update selects the
     * actor-specific cached camera joint instead of fixed joint 45; the shared
     * post-transform also applies the float at +0x18 as a rotated local
     * negative-Y offset. The leading dword's identity remains stripped.
     */
    class C_CameraFirstPerson : public C_Camera {
      public:
        ~C_CameraFirstPerson() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "FirstPerson"; }

        std::uint32_t GetState0010Raw() const { return m_state0010; }
        bool UsesDynamicJointAndOffset() const {
            return m_useDynamicJointAndOffset;
        }
        float GetLocalYOffset() const { return m_localYOffset; }

      private:
        std::uint32_t m_state0010;           // 0x10
        bool m_useDynamicJointAndOffset;     // 0x14
        std::byte m_padding0015[0x3];        // 0x15
        float m_localYOffset;                // 0x18
        std::byte m_padding001C[0x4];        // 0x1C
    };

    static_assert(sizeof(C_CameraFirstPerson) == 0x20,
                  "C_CameraFirstPerson size mismatch");
} // namespace SDK::wh::game
