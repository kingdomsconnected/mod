#pragma once

#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct UIModalDialogAmountConfirmCallbackLayout {
        std::byte m_storage[0x40];
    };

    struct UIModalDialogAmountNavigationCallbackLayout {
        std::byte m_storage[0x40];
    };

    static_assert(sizeof(UIModalDialogAmountConfirmCallbackLayout) == 0x40,
                  "amount confirm callback size mismatch");
    static_assert(sizeof(UIModalDialogAmountNavigationCallbackLayout) == 0x40,
                  "amount navigation callback size mismatch");

    /** Native numeric amount-selection dialog embedded by inventory UIs. */
    class C_UIModalDialogAmount : public C_UIFlashBase {
      public:
        ~C_UIModalDialogAmount() override = default;

        bool IsOpen() const { return m_isOpen != 0; }

      private:
        // ABI-proven std::function-like records with distinct signatures.
        UIModalDialogAmountConfirmCallbackLayout m_confirmCallback; // 0x58
        UIModalDialogAmountNavigationCallbackLayout
            m_navigationCallback;      // 0x98
        void *m_pInputContext;          // 0xD8
        std::uint8_t m_isOpen;          // 0xE0
        std::byte m_padding00E1[0x7];   // 0xE1
    };

    static_assert(sizeof(C_UIModalDialogAmount) == 0xE8,
                  "C_UIModalDialogAmount size mismatch");
} // namespace SDK::wh::guimodule
