#include "alchemy_evaluator.h"

#include "shared/game/custom_recipe_registry.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Crafting {
    bool AlchemyEvaluator::MatchesIngredient(std::string_view item, std::string_view required) {
        if (item == required) {
            return true;
        }
        for (const auto &variant : KCDC::Shared::Game::g_AlchemyHerbVariants) {
            if (variant.fresh == required && variant.dried == item) {
                return true;
            }
        }
        return false;
    }

    bool AlchemyEvaluator::Predicate(const KCDC::Shared::Game::AlchemyPredicate &predicate, const KCDC::Shared::Game::CraftRecipe &recipe, const std::vector<AlchemyResource> &resources, float tolerance, float timeUnit) {
        const auto base = std::find_if(resources.begin(), resources.end(), [](const AlchemyResource &resource) {
            return resource.base;
        });
        if (predicate.kind == KCDC::Shared::Game::AlchemyPredicateKind::PotionBase) {
            if (base == resources.end()) {
                return false;
            }
            for (const auto &entry : KCDC::Shared::Game::g_AlchemyBases) {
                if (entry.item == base->item) {
                    return entry.kind == recipe.base;
                }
            }
            return false;
        }
        if (predicate.kind == KCDC::Shared::Game::AlchemyPredicateKind::BaseDistilled) {
            return (base != resources.end() && base->distilled) == (predicate.value == 1.0F);
        }
        if (predicate.kind == KCDC::Shared::Game::AlchemyPredicateKind::ResultMilled) {
            return (base != resources.end() && base->milled) == (predicate.value == 1.0F);
        }

        std::uint32_t count = 0;
        bool allMilled      = true;
        float exposure      = 0;
        for (const auto &resource : resources) {
            if (!MatchesIngredient(resource.item, predicate.item)) {
                continue;
            }
            ++count;
            allMilled = allMilled && resource.milled;
            exposure += predicate.kind == KCDC::Shared::Game::AlchemyPredicateKind::StrongBoiling ? resource.strongSeconds : resource.weakSeconds;
        }
        switch (predicate.kind) {
        case KCDC::Shared::Game::AlchemyPredicateKind::CountEqual: return static_cast<float>(count) == predicate.value;
        case KCDC::Shared::Game::AlchemyPredicateKind::CountAtLeast: return static_cast<float>(count) >= predicate.value;
        case KCDC::Shared::Game::AlchemyPredicateKind::Milled: return (count != 0 && allMilled) == (predicate.value == 1.0F);
        case KCDC::Shared::Game::AlchemyPredicateKind::WeakBoiling:
        case KCDC::Shared::Game::AlchemyPredicateKind::StrongBoiling: {
            if (count != 0) {
                exposure /= static_cast<float>(count);
            }
            exposure /= timeUnit > 0.0F ? timeUnit : 1.0F;
            const float normalized = predicate.value > 0.0F ? exposure / predicate.value : exposure + 1.0F;
            return std::abs(1.0F - normalized) <= tolerance;
        }
        default: return false;
        }
    }

    AlchemyMatch AlchemyEvaluator::Match(const std::vector<AlchemyResource> &resources, float tolerance, float timeUnit, std::uint16_t requiredDLC) {
        AlchemyMatch result;
        if (resources.size() > 4096 || !std::isfinite(tolerance) || tolerance < 0.0F || tolerance > 1.0F || !std::isfinite(timeUnit)) {
            return result;
        }
        std::size_t bases = 0;
        for (const auto &resource : resources) {
            if (resource.base) {
                ++bases;
            }
            if (!std::isfinite(resource.weakSeconds) || !std::isfinite(resource.strongSeconds) || resource.weakSeconds < 0 || resource.strongSeconds < 0) {
                return result;
            }
        }
        if (bases > 1) {
            return result;
        }
        result.valid = true;
        KCDC::Shared::Game::CustomRecipeRegistry::Get().ForEachProgram([&](const KCDC::Shared::Game::AlchemyProgram &program, const KCDC::Shared::Game::CraftRecipe &recipe) {
            if (program.steps.empty()) {
                return false;
            }
            std::vector<std::uint32_t> passed;
            std::uint32_t xp = 0;
            for (const auto &step : program.steps) {
                const bool accepted = std::all_of(step.predicates.begin(), step.predicates.end(), [&](const KCDC::Shared::Game::AlchemyPredicate &predicate) {
                    return Predicate(predicate, recipe, resources, tolerance, timeUnit);
                });
                if (&step == &program.steps.front() && !accepted) {
                    break;
                }
                if (accepted) {
                    passed.push_back(step.id);
                    xp += step.rewardXP;
                }
            }
            if (passed.empty()) {
                return false;
            }
            result.recipe          = &recipe;
            result.contentAllowed  = KCDC::Shared::Game::CraftDLCEnabled(recipe.dlc, requiredDLC);
            result.successfulSteps = std::move(passed);
            result.totalSteps      = static_cast<std::uint32_t>(program.steps.size());
            result.stepXP          = xp;
            result.baseXP          = program.baseXP;
            return true;
        });
        return result;
    }
} // namespace KCDC::Features::Crafting
