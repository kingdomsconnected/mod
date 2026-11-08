#pragma once

#include "c_camera.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /**
     * Camera implementation used by dialog sequences.
     *
     * Its inlined construction initializes the float at +0x18 to 60 degrees
     * in radians. The view-update override delegates to the active dialog
     * camera and refreshes this value, while virtual slot 7 returns it
     * directly. The preceding qword has no proven semantic identity.
     */
    class C_CameraDialog : public C_Camera {
      public:
        ~C_CameraDialog() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "Dialog"; }

        float GetFieldOfViewRadians() const { return m_fieldOfViewRadians; }

      private:
        std::uint64_t m_state0010;   // 0x10, C_CameraManager's ctor (0x180896DC8) zeroes
        float m_fieldOfViewRadians;  // 0x18
        std::byte m_padding001C[0x4];
    };

    static_assert(sizeof(C_CameraDialog) == 0x20,
                  "C_CameraDialog size mismatch");
} // namespace SDK::wh::game
