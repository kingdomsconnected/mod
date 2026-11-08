#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct UIModalDialogRandomEventChoiceCallbackLayout {
        std::byte m_storage[0x40];
    };

    struct UIModalDialogRandomEventNavigationCallbackLayout {
        std::byte m_storage[0x40];
    };

    static_assert(sizeof(UIModalDialogRandomEventChoiceCallbackLayout) == 0x40,
                  "random-event choice callback size mismatch");
    static_assert(sizeof(UIModalDialogRandomEventNavigationCallbackLayout) == 0x40,
                  "random-event navigation callback size mismatch");

    /** Native four-choice random-event dialog embedded by the map UI. */
    class C_UIModalDialogRandomEvent : public C_UIFlashBase {
      public:
        ~C_UIModalDialogRandomEvent() override = default;

        /**
         * Raw game-ABI invocation. The arguments are native dialog-data,
         * choice-vector, and std::function-like objects rather than SDK-owned
         * C++ containers or ordinary function pointers.
         */
        void InvokeOpenRandomEventDialogRaw(const void *dialogData,
                                            const void *choices,
                                            const void *choiceCallback,
                                            const void *navigationCallback,
                                            const void *cancelCallback) {
            hook::this_call<void>(gPatterns.C_UIModalDialogRandomEvent__OpenRaw, this, dialogData, choices, choiceCallback, navigationCallback, cancelCallback);
        }

        void InvokeCloseRandomEventDialogRaw(bool value) {
            hook::this_call<void>(gPatterns.C_UIModalDialogRandomEvent__CloseRaw, this, value);
        }

        bool IsOpen() const { return m_isOpen != 0; }

      private:
        UIModalDialogRandomEventChoiceCallbackLayout
            m_choiceCallback; // 0x58
        UIModalDialogRandomEventNavigationCallbackLayout
            m_navigationCallback; // 0x98
        UIModalDialogRandomEventChoiceCallbackLayout
            m_cancelCallback;             // 0xD8
        std::uint8_t m_isOpen;             // 0x118
        std::byte m_padding0119[0x3];      // 0x119
        float m_value011C;                 // 0x11C
        float m_value0120;                 // 0x120
        float m_value0124;                 // 0x124
    };

    static_assert(sizeof(C_UIModalDialogRandomEvent) == 0x128,
                  "C_UIModalDialogRandomEvent size mismatch");
} // namespace SDK::wh::guimodule
