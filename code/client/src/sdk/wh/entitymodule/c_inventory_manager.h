#pragma once

#include "../framework/c_override_conflict_manager.h"
#include "../framework/i_wuid_mapping_provider.h"
#include "c_inventory.h"
#include "c_world_inventory.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    struct InventoryMappingSlotLayout {
        std::uint16_t m_generation; // 0x00
        std::byte m_state0002[0x6]; // 0x02
        C_Inventory *m_pInventory;  // 0x08
    };

    static_assert(sizeof(InventoryMappingSlotLayout) == 0x10, "inventory mapping slot size mismatch");

    /** Global owner of actor inventories and the unowned world inventory. */
    class C_InventoryManager: public framework::I_WUIDMappingProvider {
      public:
        ~C_InventoryManager() override = default;

        void unk_fn0000() override;
        void unk_fn0001() override;

        static C_InventoryManager *GetInstance();

        C_Inventory *InvokeFindInventoryByWuidRaw(framework::WUID wuid);

        /** The GUID is passed by value; native receives a pointer to its copy. */
        C_Inventory *GetOrCreateInventory(CryGUID guid, const char *name, bool *created = nullptr);
        void DestroyInventory(C_Inventory *inventory);

        static constexpr std::size_t InventoryMappingSlotCount = 0x10000;

        InventoryMappingSlotLayout *GetInventoryMappingSlots() {
            return m_inventoryMappingSlots;
        }

        const InventoryMappingSlotLayout *GetInventoryMappingSlots() const {
            return m_inventoryMappingSlots;
        }

        C_Inventory *GetInventoryAtMappingSlot(std::uint16_t index) const {
            return m_inventoryMappingSlots[index].m_pInventory;
        }

        C_WorldInventory *GetWorldInventory() {
            return &m_worldInventory;
        }
        const C_WorldInventory *GetWorldInventory() const {
            return &m_worldInventory;
        }

      private:
        std::byte m_mappingState000008[0x48];                                          // 0x000008
        InventoryMappingSlotLayout m_inventoryMappingSlots[InventoryMappingSlotCount]; // 0x000050
        std::byte m_mappingState100050[0x18];                                          // 0x100050
        std::uint32_t m_state100068;                                                   // 0x100068
        std::byte m_padding10006C[0x4];                                                // 0x10006C
        C_WorldInventory m_worldInventory;                                             // 0x100070
        std::byte m_inventoryIndex100218[0x68];                                        // 0x100218
    };

    static_assert(sizeof(C_InventoryManager) == 0x100280, "C_InventoryManager size mismatch");
} // namespace SDK::wh::entitymodule
