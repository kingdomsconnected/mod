#pragma once

#include "../../patterns.h"

#include "c_homogenous_item_descriptor.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <memory>

namespace SDK::wh::entitymodule {
    class C_Item;

    /** Exact-item selector used by inventory and equipment mutation APIs. */
    class C_ItemReferenceDescriptor: public C_HomogenousItemDescriptor {
      public:
        ~C_ItemReferenceDescriptor() override = default;
        std::vector<S_ItemWithAmount> unk_fn0001(const std::vector<C_Item *> &inputItems, bool nativeFlag) override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;

        C_Item *GetItem() const {
            return m_pItem;
        }

      private:
        C_Item *m_pItem; // 0x08
    };

    static_assert(sizeof(C_ItemReferenceDescriptor) == 0x10, "C_ItemReferenceDescriptor size mismatch");

    /** Creates a native descriptor selecting exactly `item`. */
    inline std::shared_ptr<C_ItemReferenceDescriptor> CreateItemReferenceDescriptor(C_Item *item) {
        std::shared_ptr<C_ItemReferenceDescriptor> result;
        hook::call<void>(gPatterns.CreateItemReferenceDescriptor, &result, item);
        return result;
    }
} // namespace SDK::wh::entitymodule
