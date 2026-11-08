#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct UIModalDialogItemCloseCallbackLayout {
        std::byte m_storage[0x40];
    };

    static_assert(sizeof(UIModalDialogItemCloseCallbackLayout) == 0x40,
                  "item-dialog close callback size mismatch");

    /** Native item-detail modal used by inventory presentation. */
    class C_UIModalDialogItem : public C_UIFlashBase {
      public:
        ~C_UIModalDialogItem() override = default;

        /**
         * Raw game-ABI invocation. Title and callback are native CryStringT
         * and std::function-like objects; item is the native inventory item.
         */
        void InvokeOpenItemDialogRaw(const void *title,
                                     void *item,
                                     int value0,
                                     int value1,
                                     const void *closeCallback) {
            hook::this_call<void>(gPatterns.C_UIModalDialogItem__OpenRaw, this, title, item, value0, value1, closeCallback);
        }

        void InvokeCloseItemDialogRaw() {
            hook::this_call<void>(gPatterns.C_UIModalDialogItem__CloseRaw, this);
        }

        bool IsOpen() const { return m_isOpen != 0; }

      private:
        UIModalDialogItemCloseCallbackLayout m_closeCallback; // 0x58
        void *m_pCloseListener;                               // 0x98
        void *m_pModalMenu;                                   // 0xA0
        std::uint8_t m_isOpen;                                // 0xA8
        std::byte m_padding00A9[0x7];                         // 0xA9
    };

    static_assert(sizeof(C_UIModalDialogItem) == 0xB0,
                  "C_UIModalDialogItem size mismatch");
} // namespace SDK::wh::guimodule
