#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct UIModalDialogCallbackLayout {
        std::byte m_storage[0x40];
    };

    static_assert(sizeof(UIModalDialogCallbackLayout) == 0x40,
                  "UIModalDialogCallbackLayout size mismatch");

    /** Native confirm/cancel question dialog embedded by menu screens. */
    class C_UIModalDialogQuestion : public C_UIFlashBase {
      public:
        ~C_UIModalDialogQuestion() override = default;

        /**
         * Raw game-ABI invocation. Arguments are native CryStringT/callback
         * objects, not C strings or ordinary C function pointers.
         */
        void InvokeOpenQuestionDialogRaw(const void *message,
                                         void *inputContext,
                                         const void *confirmCallback,
                                         const void *cancelCallback,
                                         const void *confirmLabel,
                                         const void *cancelLabel) {
            hook::this_call<void>(gPatterns.C_UIModalDialogQuestion__OpenQuestionDialog, this, message, inputContext, confirmCallback, cancelCallback, confirmLabel, cancelLabel);
        }

        bool IsOpen() const { return m_isOpen != 0; }

      private:
        UIModalDialogCallbackLayout m_confirmCallback; // 0x58
        UIModalDialogCallbackLayout m_cancelCallback;  // 0x98
        void *m_pInputContext;                          // 0xD8
        std::uint8_t m_isOpen;                          // 0xE0
        std::byte m_padding00E1[0x7];
    };

    static_assert(sizeof(C_UIModalDialogQuestion) == 0xE8,
                  "C_UIModalDialogQuestion size mismatch");
} // namespace SDK::wh::guimodule
