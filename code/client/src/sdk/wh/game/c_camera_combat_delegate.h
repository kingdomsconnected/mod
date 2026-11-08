#pragma once

#include "c_camera.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /** Game-camera delegate that forwards into the combat camera system. */
    class C_CameraCombatDelegate : public C_Camera {
      public:
        ~C_CameraCombatDelegate() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "Combat"; }

      private:
        void *m_pDelegate;            // 0x10, the camera this one forwards to; every override (e.g. 0x1804FB998) calls through it
        std::uint8_t m_flag0018;      // 0x18, C_CameraManager's ctor (0x180896DC8) zeroes
        std::byte m_padding0019[0x7]; // 0x19

        static void AssertLayout() {
            static_assert(offsetof(C_CameraCombatDelegate, m_pDelegate) == 0x10, "C_CameraCombatDelegate::m_pDelegate offset mismatch");
        }
    };

    static_assert(sizeof(C_CameraCombatDelegate) == 0x20,
                  "C_CameraCombatDelegate size mismatch");
} // namespace SDK::wh::game
