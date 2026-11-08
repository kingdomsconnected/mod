#pragma once

#include "features/player/soul_profile.h"
#include "shared/game/crafting_catalog.h"

#include <function2/function2.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /** The character inputs a workpiece reads, resolved from the player's progression. */
    struct SmithingCharacterContext {
        float skill                    = 0.0F;
        std::uint32_t maximumLockpicks = 0;
        bool superiorQuality           = false;
        bool hardenedSteel             = false;
        std::set<std::string> perks;
    };

    /** What a successful workpiece grants, before any of it reaches the inventory. */
    struct SmithingProduct {
        std::string itemClass;
        nlohmann::json metadata;
        // A quest product is never created by the native outcome; its quest is.
        bool questItem = false;
        float xp       = 0.0F;
    };

    /**
     * Stateless: the arithmetic of the native smithing outcome. Nothing here
     * reads the client; the one reported input is the workpiece quality, and
     * it only selects among the recipe's authored products.
     */
    class SmithingEvaluator final {
      public:
        static bool Evaluate(const KCDC::Features::Player::SoulSnapshot &soul, SmithingCharacterContext &context, std::string &error);
        /** C_BlacksmithRecipe::MeetsSkillRequirement 0x18119FD10 plus the recipe perk. */
        static std::optional<std::string> Admits(const KCDC::Shared::Game::CraftRecipe &recipe, const SmithingCharacterContext &character);
        /** The lowest product threshold as a quality, below which the workpiece breaks. */
        static float CriticalQuality(const KCDC::Shared::Game::CraftRecipe &recipe);
        /** The product `quality` makes, and its native tier, condition, buff and XP; nothing when it makes none. */
        static std::optional<SmithingProduct> Product(const KCDC::Shared::Game::CraftRecipe &recipe, float quality, const SmithingCharacterContext &character);
        /**
         * Units of one material a failed attempt spends. `coin` is flipped
         * only where the native share rounds down to nothing; heads spends one.
         */
        static std::uint32_t FailureLoss(const KCDC::Shared::Game::SmithingMaterialRule &rule, std::uint32_t required, const fu2::function<bool() const> &coin);
    };
} // namespace KCDC::Features::Crafting
