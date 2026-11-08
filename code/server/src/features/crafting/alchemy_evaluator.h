#pragma once

#include "shared/game/alchemy_catalog.h"
#include "shared/game/crafting_catalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /** One accepted unit; exposure is never shared with later additions. */
    struct AlchemyResource {
        std::string item;
        bool base           = false;
        bool milled         = false;
        bool distilled      = false;
        float weakSeconds   = 0;
        float strongSeconds = 0;
    };

    struct AlchemyMatch {
        bool valid                                    = false;
        bool contentAllowed                           = false;
        const KCDC::Shared::Game::CraftRecipe *recipe = nullptr;
        std::vector<std::uint32_t> successfulSteps;
        std::uint32_t totalSteps = 0;
        std::uint32_t stepXP     = 0;
        std::uint32_t baseXP     = 0;
    };

    /**
     * Stateless native recipe predicates over server-owned resource state.
     * Tolerance and time unit come from the accepted RPG state/rules. These
     * functions do not authorize actions, calculate products or grant XP.
     */
    class AlchemyEvaluator final {
      public:
        static bool MatchesIngredient(std::string_view item, std::string_view required);
        static bool Predicate(const KCDC::Shared::Game::AlchemyPredicate &predicate, const KCDC::Shared::Game::CraftRecipe &recipe, const std::vector<AlchemyResource> &resources, float tolerance, float timeUnit);
        static AlchemyMatch Match(const std::vector<AlchemyResource> &resources, float tolerance, float timeUnit, std::uint16_t requiredDLC);
    };
} // namespace KCDC::Features::Crafting
