#pragma once

#include <exception>

void NpcInventoryTests();
void NpcHarvestSyncTests();
void AnimalGamemodeTests();

MODULE(npc_inventory, {
    const auto run = [](void (*test)()) {
        try {
            test();
            return true;
        }
        catch (const std::exception &error) {
            printf("NPC inventory assertion failed: %s\n", error.what());
            return false;
        }
    };
    IT("preserves corpse stock across transfers and lease changes", { EQUALS(run(NpcInventoryTests), true); });
    IT("synchronizes harvest grants, completion and cancellation", { EQUALS(run(NpcHarvestSyncTests), true); });
    IT("prepares animal stock in the gamemode spawner", { EQUALS(run(AnimalGamemodeTests), true); });
});
