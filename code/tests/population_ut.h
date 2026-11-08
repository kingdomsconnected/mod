#pragma once

#include "features/world/world_population.h"
#include "shared/game/animal_catalog.h"

#include <string>

MODULE(population, {
    IT("refuses every catalogued native animal while allowing its replica body", {
        EQUALS(KCDC::Shared::Game::g_Animals.empty(), false);
        for (const auto &animal : KCDC::Shared::Game::g_Animals) {
            const std::string actorClass(animal.actorClass);
            const std::string nativeName(animal.name);
            const std::string replicaName = std::string(KCDC::Common::Spawn::kSpawnedActorNamePrefix) + actorClass + "_1";
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass.c_str(), nativeName.c_str()), false);
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass.c_str(), nullptr), false);
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass.c_str(), replicaName.c_str()), true);
        }
    });

    IT("covers the classes of every authored animal spawnpoint", {
        EQUALS(KCDC::Shared::Game::g_AnimalSpawnpoints.empty(), false);
        for (const auto &point : KCDC::Shared::Game::g_AnimalSpawnpoints) {
            const std::string actorClass(point.actorClass);
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass.c_str(), "native_spawn"), false);
        }
    });

    IT("refuses native humans, horses, dogs and all chicken flock variants", {
        for (const char *actorClass : {"NPC", "NPC_Female", "NPC_NAI", "Horse", "Dog", "Chickens", "ChickensBrownLight", "ChickensWhite"}) {
            for (const char *nativeName : {"level_animal", "", "KCDC", "KCD", "prefix_KCDC_Horse_1", "kcdc_Horse_1"}) {
                EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass, nativeName), false);
            }
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass, nullptr), false);
        }
        for (const char *actorClass : {"NPC", "NPC_Female", "NPC_NAI", "Horse", "Dog"}) {
            const std::string replicaName = std::string(KCDC::Common::Spawn::kSpawnedActorNamePrefix) + actorClass + "_2";
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(actorClass, replicaName.c_str()), true);
        }
    });

    IT("preserves the player, inventory previews, world objects and other wildlife", {
        for (const char *entityClass : {"Player", "PlayerFemale", "InventoryDummyPlayer", "InventoryDummyHorse", "InventoryDummyDog", "BasicEntity", "AnimalSpawner", "Birds", "BirdsTakeoff", "Fish", "Rats", "FieldMouse", "CatHolder", "Boid"}) {
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(entityClass, "level_entity"), true);
            EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(entityClass, nullptr), true);
        }
        EQUALS(KCDC::Features::World::PopulationSink::AllowsSpawn(nullptr, nullptr), true);
    });
});
