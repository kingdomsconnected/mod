#pragma once

#include "s_item_transfer_descriptor.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    class C_Human;

    /**
     * Item-pickup state owned by C_Human.
     *
     * The table at 0x183A5BB68 contains one stripped operation followed by
     * the virtual destructor. Human setup allocates exactly 0x100 bytes,
     * stores the owning C_Human at +0x08, and installs the pointer at
     * C_Human +0xA48.
     */
    class C_ItemPickUpHelper {
      public:
        virtual void unk_fn0000()     = 0;
        virtual ~C_ItemPickUpHelper() = default; // +0x08

        C_Human *GetHuman() const {
            return m_pHuman;
        }

      private:
        C_Human *m_pHuman;                          // 0x08
        std::byte m_pendingRequests0010[0x18];      // 0x10, native vector
        S_ItemTransferDescriptor m_pendingTransfer; // 0x28
        std::byte m_state00F0[0x10];                // 0xF0

        static void AssertLayout() {
            static_assert(offsetof(C_ItemPickUpHelper, m_pHuman) == 0x08, "C_ItemPickUpHelper::m_pHuman offset mismatch");
            static_assert(offsetof(C_ItemPickUpHelper, m_pendingTransfer) == 0x28, "C_ItemPickUpHelper::m_pendingTransfer offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemPickUpHelper) == 0x100, "C_ItemPickUpHelper size mismatch");
} // namespace SDK::wh::entitymodule
