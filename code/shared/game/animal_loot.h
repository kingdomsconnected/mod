#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct AnimalLootItem {
        std::string_view item;
        std::uint32_t amount;
        float fraction;
        float variation;
        bool food;
        bool harvester;
    };
    struct AnimalLootDefinition {
        std::string_view actorClass;
        std::vector<AnimalLootItem> items;
    };
    struct AnimalSoulDefinition {
        std::string_view soul;
        std::string_view actorClass;
        bool lootable;
        std::string_view name;
    };
    extern const std::vector<AnimalLootDefinition> g_AnimalLoot;
    extern const std::vector<AnimalSoulDefinition> g_AnimalSouls;
    const AnimalLootDefinition *FindAnimalLoot(std::string_view actorClass);
    const AnimalSoulDefinition *FindAnimalSoul(std::string_view soul);
    const AnimalSoulDefinition *FindAnimalSoulByName(std::string_view name);
} // namespace KCDC::Shared::Game
