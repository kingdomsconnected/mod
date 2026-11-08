#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** An animal soul and its matching native entity class, mined together. */
    struct AnimalDefinition {
        std::string_view name;
        std::string_view soulGuid;
        std::string_view actorClass;
    };

    /** A level-authored AnimalSpawner. Its position is a marker, not a ground query. */
    struct AnimalSpawnpoint {
        std::string_view level;
        std::string_view guid;
        std::string_view name;
        std::string_view layer;
        std::string_view soulGuid;
        std::string_view actorClass;
        glm::vec3 position;
        std::uint32_t count;
        std::uint32_t respawnDays;
        bool spawnInFlock;
        std::string_view spawnAreaGuid;
    };

    extern const std::vector<AnimalDefinition> g_Animals;
    extern const std::vector<AnimalSpawnpoint> g_AnimalSpawnpoints;

    const AnimalDefinition *FindAnimal(std::string_view nameOrGuid);

    /** Mammal NPCs only. Horses and companion dogs have their own features. */
    inline bool IsAnimalClass(std::string_view name) {
        return name == "SheepEwe" || name == "SheepRam" || name == "CattleCow" || name == "CattleBull" || name == "Pig" || name == "Boar" || name == "Hare" || name == "RedDeerStag" || name == "RedDeerDoe" || name == "RoeDeerBuck" || name == "RoeDeerHind" || name == "Wolf"
               || name == "WildDog";
    }

    inline bool IsPredatorClass(std::string_view name) {
        return name == "Wolf" || name == "WildDog";
    }
} // namespace KCDC::Shared::Game
