#pragma once

#include "c_item.h"
#include "c_item_collection.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Per-outfit item index built on the shared item-collection runtime. */
    class C_ItemIndexer : public C_ItemCollection {
      public:
        ~C_ItemIndexer() override = default;

        void unk_fn0000() override;
        void unk_fn0001() override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;

        std::size_t GetIndexedItemCount() const {
            return m_pItemsBegin != nullptr
                       ? static_cast<std::size_t>(m_pItemsEnd - m_pItemsBegin)
                       : 0;
        }

        C_Item **GetIndexedItemsBegin() { return m_pItemsBegin; }
        C_Item *const *GetIndexedItemsBegin() const { return m_pItemsBegin; }
        C_Item **GetIndexedItemsEnd() { return m_pItemsEnd; }
        C_Item *const *GetIndexedItemsEnd() const { return m_pItemsEnd; }

        C_Item *GetIndexedItemAt(std::size_t index) const {
            return index < GetIndexedItemCount() ? m_pItemsBegin[index]
                                                  : nullptr;
        }

        C_Item *FindIndexedItemById(std::uint64_t id) const {
            if (m_pItemsBegin == nullptr) {
                return nullptr;
            }

            for (C_Item *const *item = m_pItemsBegin; item != m_pItemsEnd;
                 ++item) {
                if (*item != nullptr && (*item)->GetId() == id) {
                    return *item;
                }
            }
            return nullptr;
        }

      private:
        std::uint64_t m_indexRegistration; // 0x70
        C_Item **m_pItemsBegin;             // 0x78
        C_Item **m_pItemsEnd;               // 0x80
        C_Item **m_pItemsCapacityEnd;       // 0x88
    };

    static_assert(sizeof(C_ItemIndexer) == 0x90,
                  "C_ItemIndexer size mismatch");
} // namespace SDK::wh::entitymodule
