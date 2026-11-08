#pragma once

#include "../framework/i_ui_hud_messages.h"
#include "c_ui_flash_base.h"

#include <cstddef>

namespace SDK::wh::guimodule {
    /** Concrete HUD message/notification UI owned by the GUI module. */
    class C_UIHudMessages : public C_UIFlashBase,
                            public framework::I_UIHudMessages {
      public:
        ~C_UIHudMessages() override = default;

        static C_UIHudMessages *GetInstance();

      private:
        void ShowInfoText(const CryStringT<char> &message,
                          int duration,
                          int priority) override;
        void HideInfoText() override;
        void Opaque02() override;
        void Opaque03() override;
        void Opaque04() override;
        void Opaque05() override;
        void Opaque06() override;

        // Constructor 0x180BC9680. The 0x20-byte blocks are built by a
        // COMDAT-folded constructor shared with SRoundedCornerRadii; their
        // real type is unproven.
        std::byte m_itemDialog[0xB0]; // C_UIModalDialogItem (its sizeof is asserted in c_ui_modal_dialog_item.h), 0x60; C_UIModalDialogItem, element "ApseModalDialog"
        std::byte m_object0110[0x60];      // 0x110; sub_18093D3AC
        std::byte m_object0170[0x38];      // 0x170; sub_180BC97DC
        std::byte m_object01A8[0x48];      // 0x1A8; sub_1819EBE48
        void *m_slots01F0[6];              // 0x1F0; sub_1803D1670 per slot
        std::byte m_object0220[0x38];      // 0x220; sub_180BC97DC
        std::byte m_blocks0258[3][0x20];   // 0x258
        std::byte m_object02B8[0x48];      // 0x2B8; sub_1819EBE48
        std::byte m_block0300[0x20];       // 0x300

        static void AssertLayout() {
            static_assert(offsetof(C_UIHudMessages, m_itemDialog) == 0x60, "C_UIHudMessages::m_itemDialog offset mismatch");
            static_assert(offsetof(C_UIHudMessages, m_object0110) == 0x110, "C_UIHudMessages::m_object0110 offset mismatch");
            static_assert(offsetof(C_UIHudMessages, m_slots01F0) == 0x1F0, "C_UIHudMessages::m_slots01F0 offset mismatch");
            static_assert(offsetof(C_UIHudMessages, m_blocks0258) == 0x258, "C_UIHudMessages::m_blocks0258 offset mismatch");
            static_assert(offsetof(C_UIHudMessages, m_block0300) == 0x300, "C_UIHudMessages::m_block0300 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIHudMessages) == 0x320,
                  "C_UIHudMessages size mismatch");
} // namespace SDK::wh::guimodule
