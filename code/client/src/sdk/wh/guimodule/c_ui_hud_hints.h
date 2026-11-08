#pragma once

#include "../playermodule/i_ui_hud_hints.h"
#include "c_ui_flash_base.h"
#include "i_ui_drag_and_drop_viewer.h"

namespace SDK::wh::guimodule {
    /** Concrete native HUD-hints UI registered with the player module. */
    class C_UIHudHints : public C_UIFlashBase,
                         public playermodule::I_UIHudHints,
                         public I_UIDragAndDropViewer {
      public:
        ~C_UIHudHints() override = default;

        static C_UIHudHints *GetInstance() {
            auto *hudHints = playermodule::I_UIHudHints::GetInstance();
            if (hudHints == nullptr) {
                return nullptr;
            }

            return reinterpret_cast<C_UIHudHints *>(
                reinterpret_cast<unsigned char *>(hudHints) - 0x58);
        }

      private:
        void Opaque00(const void *hints, std::uint32_t state) override;
        void Opaque01(float progress, int value0, int value1) override;
        void Opaque02(const void *text, int value0, int value1) override;
        void Opaque03(int value0, int value1) override;
        void Opaque04() override;
        void Opaque05() override;
        void Opaque06() override;
        void Opaque07() override;
        void Opaque08() override;
        void Opaque09() override;

        void DragOpaque00() override;
        void DragOpaque01() override;
        void DragOpaque02() override;
        void DragOpaque03() override;
        void DragOpaque04() override;
        void DragOpaque05() override;
        void DragOpaque06() override;

        void *m_pState0068; // 0x68
    };

    static_assert(sizeof(C_UIHudHints) == 0x70,
                  "C_UIHudHints size mismatch");
} // namespace SDK::wh::guimodule
