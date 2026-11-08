#pragma once

#include "c_ui_flash_object.h"
#include <cstdint>

namespace SDK::wh::guimodule {
    // Constructor 0x1808F67C4; derived selection adds its fields at 0x78.
    class C_UIFlashListItem: public C_UIFlashObject {
      public:
        ~C_UIFlashListItem() = delete;

        void SetClassId(std::uint32_t id) {
            m_classId = id;
        }

      private:
        std::uint32_t m_classId; // 0x18, Flash grouping index, not the item GUID.
        std::byte m_opaque001C[0x5C];

        static void AssertLayout() {
            static_assert(offsetof(C_UIFlashListItem, m_classId) == 0x18);
        }
    };
    static_assert(sizeof(C_UIFlashListItem) == 0x78);
} // namespace SDK::wh::guimodule
