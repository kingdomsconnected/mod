#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct AlchemyHerbClass {
        std::string_view item;
        bool decays;
    };
    extern const std::vector<AlchemyHerbClass> g_AlchemyHerbClasses;
    extern const std::vector<std::string_view> g_AlchemyCrushableSpecialIngredients;
    bool IsCrushableAlchemyIngredient(std::string_view item);
    const AlchemyHerbClass *FindAlchemyHerbClass(std::string_view item);
} // namespace KCDC::Shared::Game
