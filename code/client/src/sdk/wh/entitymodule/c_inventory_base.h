#pragma once

#include "c_item.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** RTTI-proven primary base of C_Inventory. */
    class C_InventoryBase: public C_ItemHolder {
      public:
        std::size_t GetItemCount() const {
            return m_pItemsBegin != nullptr ? static_cast<std::size_t>(m_pItemsEnd - m_pItemsBegin) : 0;
        }

        C_Item **GetItemsBegin() {
            return m_pItemsBegin;
        }
        C_Item *const *GetItemsBegin() const {
            return m_pItemsBegin;
        }
        C_Item **GetItemsEnd() {
            return m_pItemsEnd;
        }
        C_Item *const *GetItemsEnd() const {
            return m_pItemsEnd;
        }

        C_Item *GetItemAt(std::size_t index) const {
            return index < GetItemCount() ? m_pItemsBegin[index] : nullptr;
        }

        // 0x1808F0D14 registers split clones at +0x20 when this inventory
        // owns the source; +0x08 is conditional on its effective holder.
        // Owned items remain here while another holder represents them.
        std::size_t GetOwnedItemCount() const {
            return m_pOwnedItemsBegin != nullptr ? static_cast<std::size_t>(m_pOwnedItemsEnd - m_pOwnedItemsBegin) : 0;
        }

        C_Item *GetOwnedItemAt(std::size_t index) const {
            return index < GetOwnedItemCount() ? m_pOwnedItemsBegin[index] : nullptr;
        }

        C_Item *FindOwnedItemById(std::uint64_t id) const {
            for (std::size_t index = 0; index < GetOwnedItemCount(); ++index) {
                auto *item = GetOwnedItemAt(index);
                if (item != nullptr && item->GetId() == id) {
                    return item;
                }
            }
            return nullptr;
        }

        C_Item *FindItemById(std::uint64_t id) const {
            if (m_pItemsBegin == nullptr) {
                return nullptr;
            }

            for (C_Item *const *item = m_pItemsBegin; item != m_pItemsEnd; ++item) {
                if (*item != nullptr && (*item)->GetId() == id) {
                    return *item;
                }
            }
            return nullptr;
        }

      protected:
        ~C_InventoryBase() override = default;

      private:
        C_Item **m_pItemsBegin;              // 0x08
        C_Item **m_pItemsEnd;                // 0x10
        C_Item **m_pItemsCapacityEnd;        // 0x18
        C_Item **m_pOwnedItemsBegin;         // 0x20
        C_Item **m_pOwnedItemsEnd;           // 0x28
        C_Item **m_pOwnedItemsCapacityEnd;   // 0x30
        std::byte m_listenerState0038[0xB0]; // 0x38

        static void AssertLayout() {
            static_assert(offsetof(C_InventoryBase, m_pItemsBegin) == 0x08);
            static_assert(offsetof(C_InventoryBase, m_pItemsEnd) == 0x10);
            static_assert(offsetof(C_InventoryBase, m_pItemsCapacityEnd) == 0x18);
            static_assert(offsetof(C_InventoryBase, m_pOwnedItemsBegin) == 0x20);
            static_assert(offsetof(C_InventoryBase, m_pOwnedItemsEnd) == 0x28);
            static_assert(offsetof(C_InventoryBase, m_pOwnedItemsCapacityEnd) == 0x30);
            static_assert(offsetof(C_InventoryBase, m_listenerState0038) == 0x38);
        }
    };

    static_assert(sizeof(C_InventoryBase) == 0xE8, "C_InventoryBase size mismatch");
} // namespace SDK::wh::entitymodule
