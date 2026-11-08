#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include "c_inventory_base.h"
#include "c_pickable_item.h"
#include "s_item_class_guid.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    class C_ItemClassDescriptor;

    /** Global holder for items that are not currently owned by an actor. */
    class C_WorldInventory: public C_InventoryBase {
      public:
        ~C_WorldInventory() override = default;

        /** Resolves a recovery after AddItem has changed its holder and WUID. */
        C_Item *FindItemByPickable(const C_PickableItem *pickable) const {
            if (pickable == nullptr) {
                return nullptr;
            }
            for (std::size_t index = 0; index < GetOwnedItemCount(); ++index) {
                auto *item = GetOwnedItemAt(index);
                if (item != nullptr && item->GetPickableItem() == pickable) {
                    return item;
                }
            }
            return nullptr;
        }

        void unk_fn0001() override;
        void unk_fn0003() override;
        void unk_fn0009() override;
        void unk_fn0010() override;

        /** Uses the proven generic holder path behind C_Inventory::CreateItems. */
        std::shared_ptr<C_ItemClassDescriptor> CreateItems(const S_ItemClassGUID &itemClass, int amount = 1, bool showUINotification = false, std::uint32_t quality = 0, float health = -1.0F, float condition = -1.0F) {
            std::shared_ptr<C_ItemClassDescriptor> result;
            hook::this_call<void>(gPatterns.C_Inventory__CreateItems, this, &result, &itemClass, amount, showUINotification, quality, health, condition);
            return result;
        }

      private:
        std::byte m_worldRecords00E8[0x18];  // 0x0E8
        std::byte m_lookupState0100[0x40];   // 0x100
        std::byte m_entityQueue0140[0x18];   // 0x140
        std::byte m_entityIndex0158[0x10];   // 0x158
        std::byte m_entityIndex0168[0x10];   // 0x168
        std::byte m_listenerState0178[0x30]; // 0x178
    };

    static_assert(sizeof(C_WorldInventory) == 0x1A8, "C_WorldInventory size mismatch");
} // namespace SDK::wh::entitymodule
