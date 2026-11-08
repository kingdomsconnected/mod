#pragma once

#include "c_item_property_descriptor.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Category-wide inventory selector with native filter state. */
    class C_ItemCategoryDescriptor : public C_ItemPropertyDescriptor {
      public:
        ~C_ItemCategoryDescriptor() override = default;
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

        const std::byte *GetCategoryFilterStateRaw() const {
            return m_categoryFilterState0038;
        }

      private:
        std::byte m_categoryFilterState0038[0x48]; // 0x38
    };

    static_assert(sizeof(C_ItemCategoryDescriptor) == 0x80,
                  "C_ItemCategoryDescriptor size mismatch");
} // namespace SDK::wh::entitymodule
