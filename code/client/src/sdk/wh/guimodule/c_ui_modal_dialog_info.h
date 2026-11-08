#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct UIModalDialogInfoCallbackLayout {
        std::byte m_storage[0x40];
    };

    static_assert(sizeof(UIModalDialogInfoCallbackLayout) == 0x40,
                  "UIModalDialogInfoCallbackLayout size mismatch");

    /** Native one-button message/warning dialog embedded by menu screens. */
    class C_UIModalDialogInfo : public C_UIFlashBase {
      public:
        ~C_UIModalDialogInfo() override = default;

        /**
         * Raw game-ABI invocation. Arguments are native CryStringT/callback
         * objects, not C strings or ordinary C function pointers.
         */
        void InvokeOpenInfoDialogRaw(const void *message,
                                     void *inputContext,
                                     const void *closeCallback,
                                     const void *confirmLabel) {
            hook::this_call<void>(gPatterns.C_UIModalDialogInfo__OpenInfoDialog, this, message, inputContext, closeCallback, confirmLabel);
        }

        void InvokeCloseInfoDialogRaw() {
            hook::this_call<void>(gPatterns.C_UIModalDialogInfo__CloseRaw, this);
        }

        bool IsOpen() const { return m_isOpen != 0; }

      private:
        UIModalDialogInfoCallbackLayout m_closeCallback; // 0x58
        void *m_pInputContext;                            // 0x98
        std::uint8_t m_isOpen;                            // 0xA0
        std::byte m_padding00A1[0x7];
    };

    static_assert(sizeof(C_UIModalDialogInfo) == 0xA8,
                  "C_UIModalDialogInfo size mismatch");
} // namespace SDK::wh::guimodule
