#include "smithing_evaluator.h"

#include "features/player/soul_evaluator.h"
#include "shared/features/crafting/smithing_native_ids.h"
#include "shared/game/custom_recipe_registry.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/soul_rule_catalog.h"
#include "shared/game/soul_scalar.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KCDC::Features::Crafting {
    namespace {
        // ItemHealthOverlapCoef, S_Constants+0xAD4; see docs/inventory-properties.md.
        constexpr float kItemHealthOverlapCoefficient = 0.25F;
        constexpr float kExperienceUnits              = static_cast<float>(KCDC::Shared::Game::SoulNative::kExperienceUnitsPerPoint);
        constexpr float kMaximumLockpicks             = 100.0F;

        bool Fail(std::string &error, const char *reason) {
            error = reason;
            return false;
        }

        float Parameter(std::string_view name) {
            for (const auto &parameter : KCDC::Shared::Game::g_SoulRuleParameters) {
                if (parameter.name == name) {
                    return parameter.value;
                }
            }
            throw std::logic_error("Missing generated soul rule parameter");
        }

        bool Ability(const KCDC::Features::Player::SoulProfile &profile, std::uint32_t ability) {
            return std::any_of(KCDC::Shared::Game::g_SoulPerkAbilities.begin(), KCDC::Shared::Game::g_SoulPerkAbilities.end(), [&](const KCDC::Shared::Game::SoulPerkAbility &edge) {
                return edge.ability == ability && profile.perks.contains(std::string(edge.perk));
            });
        }

        /** ClampRemap_180466E28, in its native float order. */
        float ClampRemap(float value, float minimum, float maximum, float outputMinimum, float outputMaximum) {
            // An empty interval has one point; the native division would be 0/0.
            if (!(maximum > minimum)) {
                return outputMaximum;
            }
            const float clamped = std::clamp(value, minimum, maximum);
            return outputMaximum - (((maximum - clamped) * (outputMaximum - outputMinimum)) / (maximum - minimum));
        }

        /**
         * C_RPGItemHealth slot 10 0x1804672B8: the first tier, counting up,
         * whose overlapping health interval holds `health`. Unlike the minimum
         * health of an item, tier one's lower bound here is zero.
         */
        std::uint32_t TierForHealth(std::uint32_t maximumQuality, float health) {
            const float denominator = static_cast<float>(maximumQuality * (maximumQuality + 1));
            for (std::uint32_t quality = 1; quality <= maximumQuality; ++quality) {
                const float minimum = (static_cast<float>(quality * (quality - 1)) / denominator) * (1.0F - kItemHealthOverlapCoefficient);
                const float maximum = static_cast<float>(quality * (quality + 1)) / denominator;
                if (health >= minimum && health <= maximum) {
                    return quality;
                }
            }
            return 1;
        }

        /** int(value * 128 + 0.5), the native positive XP conversion 0x180487920. */
        std::uint32_t ExperienceUnits(float value) {
            return static_cast<std::uint32_t>(value * kExperienceUnits + 0.5F);
        }
    } // namespace

    bool SmithingEvaluator::Evaluate(const KCDC::Features::Player::SoulSnapshot &soul, SmithingCharacterContext &context, std::string &error) {
        if (soul.profile.health <= 0.0F) {
            return Fail(error, "soulActorNotAlive");
        }
        SmithingCharacterContext result;
        const auto level = soul.profile.skillLevels.find(KCDC::Shared::Crafting::SmithingNative::kSkill);
        const float base = level == soul.profile.skillLevels.end() ? 0.0F : static_cast<float>(level->second);
        const auto scope = KCDC::Features::Player::SoulEvaluationScope::SmithingOutcome;
        KCDC::Shared::Game::SoulScalarCalculation calculated;
        if (!KCDC::Features::Player::EvaluateCanonicalSoulScalar(soul, soul.clock, {KCDC::Shared::Game::SoulScalarDomain::Skill, KCDC::Shared::Crafting::SmithingNative::kSkill, base, std::nullopt}, calculated, error, scope)) {
            return false;
        }
        result.skill = calculated.effective;
        const KCDC::Shared::Game::SoulScalarQuery lockpicks {KCDC::Shared::Game::SoulScalarDomain::DerivedStat, KCDC::Shared::Crafting::SmithingNative::kMaximumLockpicksStat, KCDC::Shared::Crafting::SmithingNative::kMaximumLockpicksBase, std::nullopt};
        if (!KCDC::Features::Player::EvaluateCanonicalSoulScalar(soul, soul.clock, lockpicks, calculated, error, scope)) {
            return false;
        }
        // RPG slot 73 truncates the stat to int before drawing 1..n. The cap
        // is the server's bound on one grant, not a native limit.
        result.maximumLockpicks = calculated.effective >= 1.0F ? static_cast<std::uint32_t>(std::min(calculated.effective, kMaximumLockpicks)) : 0;
        result.superiorQuality  = Ability(soul.profile, KCDC::Shared::Crafting::SmithingNative::kSuperiorQualityAbility);
        result.hardenedSteel    = Ability(soul.profile, KCDC::Shared::Crafting::SmithingNative::kHardenedSteelAbility);
        result.perks            = soul.profile.perks;
        context                 = std::move(result);
        error.clear();
        return true;
    }

    std::optional<std::string> SmithingEvaluator::Admits(const KCDC::Shared::Game::CraftRecipe &recipe, const SmithingCharacterContext &character) {
        if (!recipe.perk.empty() && !character.perks.contains(std::string(recipe.perk))) {
            return "recipeNotKnown";
        }
        if (character.skill < static_cast<float>(recipe.minSkill)) {
            return "skillTooLow";
        }
        return std::nullopt;
    }

    float SmithingEvaluator::CriticalQuality(const KCDC::Shared::Game::CraftRecipe &recipe) {
        // 0x1812C106C through 0x182006CE0 scans every row for the smallest.
        std::uint32_t threshold = 100;
        for (const auto &product : recipe.products) {
            threshold = std::min(threshold, product.threshold);
        }
        return static_cast<float>(threshold) / 100.0F;
    }

    std::optional<SmithingProduct> SmithingEvaluator::Product(const KCDC::Shared::Game::CraftRecipe &recipe, float quality, const SmithingCharacterContext &character) {
        if (recipe.kind != KCDC::Shared::Game::CraftKind::Smithing || !std::isfinite(quality) || quality < 0.0F || quality > 1.0F) {
            return std::nullopt;
        }
        // 0x182E58CE0 truncates the quality to a percent and takes the
        // highest threshold it reaches.
        const auto percent                               = static_cast<std::uint32_t>(quality * 100.0F);
        const KCDC::Shared::Game::CraftProduct *selected = nullptr;
        for (const auto &product : recipe.products) {
            if (product.threshold <= percent && (selected == nullptr || product.threshold > selected->threshold)) {
                selected = &product;
            }
        }
        if (selected == nullptr) {
            return std::nullopt;
        }
        const auto product     = KCDC::Shared::Game::CustomRecipeRegistry::Get().ProductClass(selected->item);
        const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(selected->item);
        if (!product || properties == nullptr) {
            return std::nullopt;
        }
        SmithingProduct result;
        result.itemClass = std::string(selected->item);
        result.questItem = product->questItem;
        // RPG slot 73: the finishing XP is quality in 1/128 units times the
        // fixed-point parameter, shifted back by seven bits.
        const std::uint64_t units = (static_cast<std::uint64_t>(ExperienceUnits(quality)) * ExperienceUnits(Parameter("BlacksmithingFinishingXP"))) >> 7;
        result.xp                 = static_cast<float>(units) / kExperienceUnits;
        // RPG slot 75: the tier the workpiece's health above the critical
        // threshold reaches, held at three without ability 65.
        const float critical = CriticalQuality(recipe);
        const float health   = ClampRemap(quality, critical, 1.0F, 0.0F, 1.0F);
        auto tier            = TierForHealth(properties->maximumQuality, health);
        if (!character.superiorQuality) {
            tier = std::min(tier, KCDC::Shared::Crafting::SmithingNative::kHighestTierWithoutAbility);
        }
        nlohmann::json metadata = {{"quality", tier}, {"condition", 1.0F}};
        // A quest product is never created -- natively its quest makes it,
        // and the inventory refuses quest items -- so only its grade is
        // reported, without the item properties nothing will hold.
        if (result.questItem) {
            result.metadata = {{"quality", tier}};
            return result;
        }
        // RPG slot 76: condition within the tier, remapped into the authored
        // condition range. Every shipped product whose health can change is a
        // melee weapon; a horseshoe keeps full condition.
        if (properties->canHealthChange) {
            if (product->kind != KCDC::Shared::Game::SmithingProductKind::MeleeWeapon) {
                return std::nullopt;
            }
            if (product->weaponClass != KCDC::Shared::Crafting::SmithingNative::kConditionExemptWeaponClass) {
                nlohmann::json worst, best;
                std::string error;
                if (!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(result.itemClass, {{"quality", tier}, {"condition", 0.0F}}, worst, error)
                    || !KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(result.itemClass, {{"quality", tier}, {"condition", 1.0F}}, best, error)) {
                    return std::nullopt;
                }
                const float relative  = ClampRemap(health, worst.at("health").get<float>(), best.at("health").get<float>(), 0.0F, 1.0F);
                metadata["condition"] = ClampRemap(relative, 0.0F, 1.0F, Parameter("BlacksmithingMinCondition"), Parameter("BlacksmithingMaxCondition"));
            }
        }
        // RPG slot 74: Hardened Steel on a weapon, with the perk's ability.
        if (character.hardenedSteel && product->kind == KCDC::Shared::Game::SmithingProductKind::MeleeWeapon) {
            metadata["onEquipBuffs"] = nlohmann::json::array({KCDC::Shared::Inventory::kHardenedSteelItemBuff});
        }
        std::string error;
        if (!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(result.itemClass, metadata, result.metadata, error)) {
            return std::nullopt;
        }
        return result;
    }

    std::uint32_t SmithingEvaluator::FailureLoss(const KCDC::Shared::Game::SmithingMaterialRule &rule, std::uint32_t required, const fu2::function<bool() const> &coin) {
        // ConsumeRecipeMaterials 0x1812C1950, failure branch.
        if (rule.questItem || !rule.divisible) {
            return 0;
        }
        const auto loss = static_cast<std::uint32_t>(static_cast<float>(required) * KCDC::Shared::Crafting::SmithingNative::kFailureMaterialLoss);
        return loss != 0 ? loss : (coin() ? 1U : 0U);
    }
} // namespace KCDC::Features::Crafting
