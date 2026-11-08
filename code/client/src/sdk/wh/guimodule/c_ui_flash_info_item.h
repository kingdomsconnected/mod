#pragma once

#include "c_ui_flash_object.h"

namespace SDK::wh::guimodule {
    /** Constructor 0x18055E528; shared allocation 0x1816F30C0 is 0x50 + 0x10. */
    class C_UIFlashInfoItem: public C_UIFlashObject {
      public:
        ~C_UIFlashInfoItem() = delete;

        void SetDescription(const char *key) {
            m_description.Assign(key);
        }

      private:
        std::byte m_opaque0018[0x10];
        CryStringT<char> m_description;
        std::byte m_opaque0030[0x20];

        static void AssertLayout() {
            static_assert(offsetof(C_UIFlashInfoItem, m_description) == 0x28);
        }
    };
    static_assert(sizeof(C_UIFlashInfoItem) == 0x50);
} // namespace SDK::wh::guimodule
