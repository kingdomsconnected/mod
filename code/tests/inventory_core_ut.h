#pragma once

#include <exception>

void InventoryModelTests();
void CustomItemTests();
void CustomItemBindingTests();
void InventorySyncTests();
void InventoryReconciliationTests();
void InventoryPropertyPolicyTests();
void InventoryGameplayTests();

MODULE(inventory_core, {
    const auto run = [](void (*test)()) {
        try {
            test();
            return true;
        }
        catch (const std::exception &error) {
            printf("Inventory assertion failed: %s\n", error.what());
            return false;
        }
    };
    IT("passes the inventory model checks", { EQUALS(run(InventoryModelTests), true); });
    IT("passes the custom item checks", { EQUALS(run(CustomItemTests), true); });
    IT("passes the custom item binding checks", { EQUALS(run(CustomItemBindingTests), true); });
    IT("passes the inventory sync checks", { EQUALS(run(InventorySyncTests), true); });
    IT("passes the inventory reconciliation checks", { EQUALS(run(InventoryReconciliationTests), true); });
    IT("passes the inventory property policy checks", { EQUALS(run(InventoryPropertyPolicyTests), true); });
    IT("passes the inventory gameplay checks", { EQUALS(run(InventoryGameplayTests), true); });
});
