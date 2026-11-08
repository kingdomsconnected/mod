#include "alchemy_character_evaluator.h"

#include "shared/game/soul_rule_catalog.h"

#include <algorithm>
#include <stdexcept>

namespace KCDC::Features::Crafting {
    namespace {
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

        bool Apply(const std::vector<KCDC::Shared::Game::SoulScalarTerm> &terms, std::uint32_t modifier, float base, float &result, std::string &error) {
            const auto calculated = KCDC::Shared::Game::EvaluateResolvedSoulScalar({KCDC::Shared::Game::SoulScalarDomain::Adjustment, modifier, base, std::nullopt}, terms, error);
            if (!calculated) {
                return false;
            }
            result = calculated->effective;
            return true;
        }
    } // namespace

    bool AlchemyCharacterEvaluator::Evaluate(const KCDC::Features::Player::SoulSnapshot &soul, AlchemyCharacterContext &context, std::string &error) {
        if (soul.profile.health <= 0.0F) {
            return Fail(error, "soulActorNotAlive");
        }
        AlchemyCharacterContext result;
        const auto level = soul.profile.skillLevels.find(KCDC::Shared::Crafting::AlchemyNative::kSkill);
        const float base = level == soul.profile.skillLevels.end() ? 0.0F : static_cast<float>(level->second);
        const auto scope = KCDC::Features::Player::SoulEvaluationScope::AlchemyOutcome;
        const KCDC::Shared::Game::SoulScalarQuery skill {KCDC::Shared::Game::SoulScalarDomain::Skill, KCDC::Shared::Crafting::AlchemyNative::kSkill, base, std::nullopt};
        const KCDC::Shared::Game::SoulScalarQuery bonus {KCDC::Shared::Game::SoulScalarDomain::DerivedStat, KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusStat, KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusBase, std::nullopt};
        KCDC::Shared::Game::SoulScalarCalculation calculated;
        if (!KCDC::Features::Player::EvaluateCanonicalSoulScalar(soul, soul.clock, skill, calculated, error, scope)) {
            return false;
        }
        result.skill         = calculated.effective;
        result.relativeSkill = *calculated.relative;
        if (!KCDC::Features::Player::EvaluateCanonicalSoulScalar(soul, soul.clock, bonus, calculated, error, scope)) {
            return false;
        }
        result.sessionQualityBonus = calculated.effective;
        const auto adjustment      = KCDC::Shared::Game::SoulScalarDomain::Adjustment;
        if (!KCDC::Features::Player::ResolveCanonicalSoulTerms(soul, soul.clock, adjustment, KCDC::Shared::Crafting::AlchemyNative::kYieldModifier, result.yieldTerms, error, scope)
            || !KCDC::Features::Player::ResolveCanonicalSoulTerms(soul, soul.clock, adjustment, KCDC::Shared::Crafting::AlchemyNative::kScoreModifier, result.scoreTerms, error, scope)) {
            return false;
        }
        result.trialAndError      = Ability(soul.profile, KCDC::Shared::Crafting::AlchemyNative::kTrialAndErrorAbility);
        result.bundleAlchemist    = Ability(soul.profile, KCDC::Shared::Crafting::AlchemyNative::kBundleAlchemistAbility);
        result.secretOfSecrets    = Ability(soul.profile, KCDC::Shared::Crafting::AlchemyNative::kSecretOfSecretsAbility);
        result.experimentalPowder = Ability(soul.profile, KCDC::Shared::Crafting::AlchemyNative::kExperimentalPowderAbility);
        result.allowedMistakes    = static_cast<std::uint32_t>(Parameter("AlchemyRecipeStepsTolerance")) + (result.trialAndError ? static_cast<std::uint32_t>(Parameter("AlchemyTrialEndErrorPerkTolerance")) : 0);
        const float toleranceBase = Parameter("AlchemyToleranceBase");
        result.boilingTolerance   = std::clamp(toleranceBase + result.relativeSkill * (Parameter("AlchemyToleranceMaxLevel") - toleranceBase), 0.0F, 1.0F);
        result.freshHerbQuality   = Parameter("AlchemyFreshHerbQuality");
        result.driedHerbQuality   = Parameter("AlchemyDryHerbQuality");
        result.decayedHerbQuality = Parameter("AlchemyDecayedHerbQuality");
        result.failureXPUnits     = static_cast<std::uint32_t>(Parameter("AlchemyXPPerFailedBrewing") * kExperienceUnitsPerPoint);
        result.bundleExtraYield   = static_cast<std::uint32_t>(Parameter("BundleAlchemistPerkAdd"));
        context                   = std::move(result);
        error.clear();
        return true;
    }

    bool AlchemyCharacterEvaluator::ApplyYield(const AlchemyCharacterContext &context, float base, float &result, std::string &error) {
        return Apply(context.yieldTerms, KCDC::Shared::Crafting::AlchemyNative::kYieldModifier, base, result, error);
    }

    bool AlchemyCharacterEvaluator::ApplyScore(const AlchemyCharacterContext &context, float base, float &result, std::string &error) {
        return Apply(context.scoreTerms, KCDC::Shared::Crafting::AlchemyNative::kScoreModifier, base, result, error);
    }

    nlohmann::json AlchemyCharacterEvaluator::EncodeProjection(const AlchemyCharacterContext &context, const nlohmann::json &knowledge) {
        return {{"skill", context.skill}, {"relativeSkill", context.relativeSkill}, {"sessionQualityBonus", context.sessionQualityBonus},
            {"abilities", {{std::to_string(KCDC::Shared::Crafting::AlchemyNative::kTrialAndErrorAbility), context.trialAndError}, {std::to_string(KCDC::Shared::Crafting::AlchemyNative::kBundleAlchemistAbility), context.bundleAlchemist},
                              {std::to_string(KCDC::Shared::Crafting::AlchemyNative::kSecretOfSecretsAbility), context.secretOfSecrets}, {std::to_string(KCDC::Shared::Crafting::AlchemyNative::kExperimentalPowderAbility), context.experimentalPowder}}},
            {"knowledge", knowledge}};
    }
} // namespace KCDC::Features::Crafting
