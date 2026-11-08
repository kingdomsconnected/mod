#include <utils/safe_win32.h>

#include <iostream>
#include <stdexcept>

void GroundStockTests();
void CustomItemWorldTests();
void DropSelectionTests();
void GroundItemReplicationTests();
void StashInventoryTests();
void StashLidTests();
void GatheringTests();
void ItemCarryTests();

int RunWorldInventorySuite() {
    try {
        GroundStockTests();
        CustomItemWorldTests();
        DropSelectionTests();
        GroundItemReplicationTests();
        StashInventoryTests();
        StashLidTests();
        GatheringTests();
        ItemCarryTests();
        std::cout << "World inventory tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
