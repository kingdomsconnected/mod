#pragma once

#include "c_homogenous_item_descriptor.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Common property descriptor copied into all specialized item descriptors. */
    class C_ItemPropertyDescriptor : public C_HomogenousItemDescriptor {
      public:
        ~C_ItemPropertyDescriptor() override = default;
        std::vector<S_ItemWithAmount>
        unk_fn0001(const std::vector<C_Item *> &inputItems,
                   bool nativeFlag) override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;

      private:
        std::byte m_propertyFilterState0008[0x30]; // 0x08
    };

    static_assert(sizeof(C_ItemPropertyDescriptor) == 0x38,
                  "C_ItemPropertyDescriptor size mismatch");
} // namespace SDK::wh::entitymodule
