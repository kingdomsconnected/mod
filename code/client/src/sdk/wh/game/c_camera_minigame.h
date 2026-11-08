#pragma once

#include "c_camera.h"

#include <cstddef>

namespace SDK::wh::game {
    /** Camera implementation used by minigames. */
    class C_CameraMinigame : public C_Camera {
      public:
        ~C_CameraMinigame() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "Minigame"; }

      private:
        void *m_pMinigameSystem; // 0x10, cached by GetMinigameSystem (0x1809E5FA0) from C_WHGameModules +0x128 on first use

        static void AssertLayout() {
            static_assert(offsetof(C_CameraMinigame, m_pMinigameSystem) == 0x10, "C_CameraMinigame::m_pMinigameSystem offset mismatch");
        }
    };

    static_assert(sizeof(C_CameraMinigame) == 0x18,
                  "C_CameraMinigame size mismatch");
} // namespace SDK::wh::game
