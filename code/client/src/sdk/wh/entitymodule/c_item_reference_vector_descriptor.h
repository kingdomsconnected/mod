#pragma once

#include "../../patterns.h"

#include "c_homogenous_item_descriptor.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace SDK::wh::entitymodule {
    class C_Item;

    /** Multi-item selector owning a native copy of exact item references. */
    class C_ItemReferenceVectorDescriptor: public C_HomogenousItemDescriptor {
      public:
        ~C_ItemReferenceVectorDescriptor() override = default;
        std::vector<S_ItemWithAmount> unk_fn0001(const std::vector<C_Item *> &inputItems, bool nativeFlag) override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;

        const std::vector<C_Item *> &GetItems() const {
            return m_items;
        }
        std::size_t GetItemCount() const {
            return m_items.size();
        }

      private:
        std::vector<C_Item *> m_items; // 0x08
    };

    static_assert(sizeof(C_ItemReferenceVectorDescriptor) == 0x20, "C_ItemReferenceVectorDescriptor size mismatch");

    /** Creates a native descriptor selecting every item in `items`. */
    inline std::shared_ptr<C_ItemReferenceVectorDescriptor> CreateItemReferenceVectorDescriptor(const std::vector<C_Item *> &items) {
        std::shared_ptr<C_ItemReferenceVectorDescriptor> result;
        hook::call<void>(gPatterns.CreateItemReferenceVectorDescriptor, &result, items);
        return result;
    }
} // namespace SDK::wh::entitymodule
