#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** Native table inputs, before condition, perks and soul modifiers. */
    struct ConsumableEntry {
        std::string_view item;
        std::string_view buff;
        bool potion;
        float health;
        float energy;
        float nourishment;
        float alcohol;
    };
    extern const std::vector<ConsumableEntry> g_Consumables;
    const ConsumableEntry *FindConsumable(std::string_view item);
} // namespace KCDC::Shared::Game
