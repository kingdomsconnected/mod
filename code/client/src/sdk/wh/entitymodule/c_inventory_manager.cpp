#include "c_inventory_manager.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_InventoryManager *C_InventoryManager::GetInstance() {
        return *reinterpret_cast<C_InventoryManager **>(gPatterns.C_InventoryManager__Instance);
    }

    C_Inventory *C_InventoryManager::InvokeFindInventoryByWuidRaw(framework::WUID wuid) {
        return hook::this_call<C_Inventory *>(gPatterns.C_InventoryManager__FindInventoryByWuidRaw, this, wuid);
    }

    C_Inventory *C_InventoryManager::GetOrCreateInventory(CryGUID guid, const char *name, bool *created) {
        return hook::this_call<C_Inventory *>(gPatterns.C_InventoryManager__GetOrCreateInventory, this, &guid, name, created);
    }

    void C_InventoryManager::DestroyInventory(C_Inventory *inventory) {
        hook::this_call<void>(gPatterns.C_InventoryManager__DestroyInventory, this, inventory);
    }
} // namespace SDK::wh::entitymodule
