#include "alchemy_module.h"
#include "features/smart_object/smart_object_service.h"

#include "alchemy_evaluator.h"
#include "shared/features/crafting/alchemy_thermal.h"
#include "shared/game/alchemy_animations.h"
#include "shared/game/alchemy_catalog.h"
#include "shared/game/alchemy_ingredients.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/custom_recipe_registry.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr const char *kFailedPotion = "006ec8d6-1ce1-4e90-8267-7c349812ddcd";
        constexpr float kDecayedCondition   = 0.5F; // 1806C4184 -> 1806C40D4.
        constexpr const char *kReason       = "alchemy";

        nlohmann::json Rules(const AlchemyRules &rules) {
            return {{"boilingTolerance", rules.boilingTolerance}, {"freshHerbQuality", rules.freshHerbQuality}, {"driedHerbQuality", rules.driedHerbQuality}, {"decayedHerbQuality", rules.decayedHerbQuality}, {"failureXP", rules.failureXP},
                {"allowedMistakes", rules.allowedMistakes}, {"timeUnitSeconds", rules.timeUnitSeconds}, {"weakBoilTimeout", rules.weakBoilTimeout}, {"strongBoilTimeout", rules.strongBoilTimeout}, {"baseQualityWeight", rules.baseQualityWeight},
                {"resourceQualityWeight", rules.resourceQualityWeight}, {"firstQualityThreshold", rules.firstQualityThreshold}, {"secondQualityThreshold", rules.secondQualityThreshold}, {"secondQualityXPMultiplier", rules.secondQualityXPMultiplier},
                {"thirdQualityXPMultiplier", rules.thirdQualityXPMultiplier}, {"distillationPumps", rules.distillationPumps}};
        }

        /** The character's half of the rules; the table's half never changes within a session. */
        void RefreshRules(nlohmann::json &rules, const AlchemyCharacterContext &character) {
            rules["boilingTolerance"]   = character.boilingTolerance;
            rules["freshHerbQuality"]   = character.freshHerbQuality;
            rules["driedHerbQuality"]   = character.driedHerbQuality;
            rules["decayedHerbQuality"] = character.decayedHerbQuality;
            rules["failureXP"]          = static_cast<float>(character.failureXPUnits) / AlchemyCharacterEvaluator::kExperienceUnitsPerPoint;
            rules["allowedMistakes"]    = character.allowedMistakes;
        }

        bool Accessible(const AlchemyActor &actor, const KCDC::Shared::Game::CraftStation &station) {
            const double x        = static_cast<double>(actor.position.x) - station.x;
            const double y        = static_cast<double>(actor.position.y) - station.y;
            const double z        = static_cast<double>(actor.position.z) - station.z;
            const double distance = x * x + y * y + z * z;
            return actor.canAct && actor.level == station.level && station.kind == KCDC::Shared::Game::CraftKind::Alchemy && std::isfinite(distance) && distance <= AlchemyModule::kUseDistance * AlchemyModule::kUseDistance;
        }

        /** The player can still use the table this session was opened at. */
        bool Authorized(const AlchemyActor &actor, const nlohmann::json &state) {
            const auto *station = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(state.at("station").get<std::string>());
            return station != nullptr && Accessible(actor, *station) && state.at("virtualWorld") == actor.virtualWorld && state.at("requiredDLC") == actor.requiredDLC;
        }

        bool IngredientHand(unsigned container) {
            return container >= static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Herb1) && container <= static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::SpecialIngredient3);
        }

        bool RefundableContainer(const nlohmann::json &resources, unsigned container, bool terminal = true) {
            // Reset 1806C4F1C refunds only wholly unmilled groups 0,2,7,8,9.
            // Special hands 10..12 can explicitly return before processing,
            // but Reset destroys them.
            const bool eligible = container == static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Bowl) || container == static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Mortar)
                                  || (container >= static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Herb1) && container <= static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Herb3)) || (!terminal && IngredientHand(container));
            return eligible && std::none_of(resources.begin(), resources.end(), [&](const nlohmann::json &resource) {
                return resource.at("container") == container && resource.at("milled").get<bool>();
            });
        }

        std::vector<std::uint32_t> TerminalResourceIds(const nlohmann::json &resources) {
            std::vector<std::uint32_t> result;
            for (const auto &resource : resources) {
                if (!resource.at("base").get<bool>() && !RefundableContainer(resources, resource.at("container").get<unsigned>())) {
                    result.push_back(resource.at("id").get<std::uint32_t>());
                }
            }
            return result;
        }

        bool AllowedIngredient(const std::string &item, std::uint16_t dlc) {
            bool allowed = false;
            KCDC::Shared::Game::CustomRecipeRegistry::Get().ForEach([&](const KCDC::Shared::Game::CraftRecipe &recipe, KCDC::Shared::Game::RecipeSource) {
                if (allowed || recipe.kind != KCDC::Shared::Game::CraftKind::Alchemy || !KCDC::Shared::Game::CraftDLCEnabled(recipe.dlc, dlc)) {
                    return;
                }
                allowed = std::any_of(recipe.ingredients.begin(), recipe.ingredients.end(), [&item](const KCDC::Shared::Game::CraftMaterial &ingredient) {
                    return AlchemyEvaluator::MatchesIngredient(item, ingredient.item);
                });
            });
            return allowed;
        }

        float IngredientQuality(const std::string &itemClass, const nlohmann::json &metadata, const nlohmann::json &rules) {
            const auto *herb = KCDC::Shared::Game::FindAlchemyHerbClass(itemClass);
            if (herb == nullptr) {
                return 1.0F;
            }
            if (!herb->decays) {
                return rules.at("driedHerbQuality").get<float>();
            }
            // Canonical metadata leaves a full condition out.
            return rules.at(metadata.value("condition", 1.0F) < kDecayedCondition ? "decayedHerbQuality" : "freshHerbQuality").get<float>();
        }

        bool InContainer(const nlohmann::json &resource, KCDC::Shared::Crafting::AlchemyPosition container) {
            return resource.at("container").get<unsigned>() == static_cast<unsigned>(container);
        }

        nlohmann::json PotResourceIds(const nlohmann::json &resources) {
            nlohmann::json ids = nlohmann::json::array();
            for (const auto &resource : resources) {
                if (InContainer(resource, KCDC::Shared::Crafting::AlchemyPosition::Pot)) {
                    ids.push_back(resource.at("id"));
                }
            }
            return ids;
        }

        void Advance(nlohmann::json &state, std::uint64_t nowMs) {
            auto &thermal    = state.at("thermal");
            auto &boundaries = thermal.at("boundaries");
            auto cursor      = state.at("lastStepMs").get<std::uint64_t>();
            if (nowMs < cursor) {
                throw std::runtime_error("Backwards alchemy simulation clock");
            }
            float heat        = state.at("heat").get<float>();
            const auto &rules = state.at("rules");
            while (cursor < nowMs || (!boundaries.empty() && boundaries.front().at("atMs").get<std::uint64_t>() <= cursor)) {
                if (!boundaries.empty() && boundaries.front().at("atMs").get<std::uint64_t>() <= cursor) {
                    const auto boundary       = boundaries.front();
                    thermal["onFire"]         = boundary.at("onFire");
                    thermal["potResourceIds"] = boundary.at("potResourceIds");
                    boundaries.erase(boundaries.begin());
                    continue;
                }
                const auto end     = boundaries.empty() ? nowMs : std::min(nowMs, boundaries.front().at("atMs").get<std::uint64_t>());
                const auto elapsed = std::min(end - cursor, KCDC::Shared::Crafting::AlchemyThermal::kAuthorityStepMilliseconds);
                const bool onFire  = thermal.at("onFire").get<bool>();
                const bool filled  = !thermal.at("potResourceIds").empty();
                const auto step    = KCDC::Shared::Crafting::AlchemyThermal::Step(heat, static_cast<float>(elapsed) / 1000.0F, onFire, filled, rules.at("weakBoilTimeout").get<float>(), rules.at("strongBoilTimeout").get<float>());
                if (!step) {
                    throw std::runtime_error("Invalid alchemy thermal state");
                }
                heat             = step->heat;
                state["boiling"] = static_cast<unsigned>(step->boiling);
                for (auto &resource : state.at("resources")) {
                    if (std::find(thermal.at("potResourceIds").begin(), thermal.at("potResourceIds").end(), resource.at("id")) == thermal.at("potResourceIds").end()) {
                        continue;
                    }
                    resource["weakSeconds"]   = resource.at("weakSeconds").get<float>() + step->weakSeconds;
                    resource["strongSeconds"] = resource.at("strongSeconds").get<float>() + step->strongSeconds;
                }
                cursor += elapsed;
            }
            state["heat"]       = heat;
            state["lastStepMs"] = nowMs;
        }

        std::string AuthoredClip(const AlchemyAction &resolved, const nlohmann::json &before) {
            std::string clip;
            const auto held     = static_cast<KCDC::Shared::Crafting::AlchemyPosition>(before.at("held").get<unsigned>());
            const bool up       = !before.at("onFire").get<bool>();
            const auto slotClip = [&](KCDC::Shared::Crafting::AlchemyPosition position, bool returning) {
                const auto value = static_cast<unsigned>(position);
                if (position >= KCDC::Shared::Crafting::AlchemyPosition::Herb1 && position <= KCDC::Shared::Crafting::AlchemyPosition::Herb3) {
                    return std::string(returning ? "alchemy_return_herb_" : "alchemy_pick_herb_") + std::to_string(value - static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Herb1) + 1);
                }
                if (position >= KCDC::Shared::Crafting::AlchemyPosition::SpecialIngredient1 && position <= KCDC::Shared::Crafting::AlchemyPosition::SpecialIngredient3) {
                    return std::string(returning ? "alchemy_return_spec_ingredience_v0" : "alchemy_get_spec_ingredience_v0") + std::to_string(value - static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::SpecialIngredient1) + 1);
                }
                return std::string();
            };
            switch (resolved.kind) {
            case AlchemyActionKind::TakeIngredient: clip = slotClip(resolved.destination, false); break;
            case AlchemyActionKind::ReturnIngredient: clip = slotClip(held, true); break;
            case AlchemyActionKind::SelectBase: {
                // C_UseResourceAction constructor 1806C2B08 maps native
                // Glass1..4 to spiritus/oil/wine/water; authored ADB uses
                // vial4/vial3/vial2/vial1 respectively.
                const unsigned vial = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Glass4) - static_cast<unsigned>(resolved.destination) + 1;
                clip                = "alchemy_pick_vial_" + std::to_string(vial) + (up ? "_up" : "");
                break;
            }
            case AlchemyActionKind::MoveContents: {
                const std::string from = resolved.source == KCDC::Shared::Crafting::AlchemyPosition::Bowl     ? "bowl"
                                         : resolved.source == KCDC::Shared::Crafting::AlchemyPosition::Mortar ? "mortar"
                                         : resolved.source <= KCDC::Shared::Crafting::AlchemyPosition::Herb3  ? "herb"
                                                                                                              : "spec_ingredience";
                const std::string to   = resolved.destination == KCDC::Shared::Crafting::AlchemyPosition::Bowl ? "bowl" : resolved.destination == KCDC::Shared::Crafting::AlchemyPosition::Mortar ? "mortar" : "pot";
                clip                   = "alchemy_" + from + "_to_" + to + (to == "pot" && up ? "_up" : "");
                break;
            }
            case AlchemyActionKind::Mill: clip = "alchemy_use_mortar"; break;
            case AlchemyActionKind::TakeObject:
                clip = resolved.destination == KCDC::Shared::Crafting::AlchemyPosition::Bellows ? "alchemy_pick_bellow" : resolved.destination == KCDC::Shared::Crafting::AlchemyPosition::Bowl ? "alchemy_pick_bowl" : "alchemy_pick_potion_flask";
                break;
            case AlchemyActionKind::ReturnObject:
                clip = held == KCDC::Shared::Crafting::AlchemyPosition::Bellows  ? "alchemy_return_bellow"
                       : held == KCDC::Shared::Crafting::AlchemyPosition::Bowl   ? "alchemy_return_bowl"
                       : held == KCDC::Shared::Crafting::AlchemyPosition::Mortar ? "alchemy_return_mortar"
                                                                                 : "alchemy_return_potion_flask";
                break;
            case AlchemyActionKind::PutPotOnFire: clip = "alchemy_pot_move_down"; break;
            case AlchemyActionKind::RemovePotFromFire: clip = "alchemy_pot_move_up"; break;
            case AlchemyActionKind::FinishFromPot: clip = "alchemy_finish_pot" + std::string(up ? "_up" : ""); break;
            case AlchemyActionKind::FinishFromMortar: clip = "alchemy_gunpowder_finish" + std::string(up ? "_up" : ""); break;
            case AlchemyActionKind::BeginDistillation: clip = "alchemy_distillation_pot" + std::string(up ? "_up" : ""); break;
            case AlchemyActionKind::Pulse: clip = before.contains("distillation") ? "alchemy_use_bellow_distillation" : "alchemy_use_bellow"; break;
            case AlchemyActionKind::Wait: clip = resolved.destination == KCDC::Shared::Crafting::AlchemyPosition::Sandglass ? "alchemy_use_sandglass" : "alchemy_use_alchemy_book"; break;
            default: throw std::runtime_error("Unsupported native alchemy duration");
            }
            return clip;
        }

        std::uint64_t ClipMilliseconds(float seconds) {
            return static_cast<std::uint64_t>(std::ceil(static_cast<double>(seconds) * 1000.0));
        }

        std::uint64_t AuthoredDuration(const AlchemyAction &resolved, const nlohmann::json &before) {
            const auto *definition = KCDC::Shared::Game::FindAlchemyAnimationClip(AuthoredClip(resolved, before));
            if (definition == nullptr) {
                throw std::runtime_error("Missing authored native alchemy clip");
            }
            // Admission uses one full authored clip at scale one. Composite
            // or looping native actions may take longer; the owner's claim
            // never shortens this server-owned minimum.
            auto duration = ClipMilliseconds(definition->durationSeconds);
            if (resolved.kind == AlchemyActionKind::Mill) {
                const auto *pick = KCDC::Shared::Game::FindAlchemyAnimationClip("alchemy_pick_mortar");
                if (pick == nullptr) {
                    throw std::runtime_error("Missing authored mortar pickup clip");
                }
                duration = duration * KCDC::Shared::Crafting::AlchemyNative::kMortarUseCount + ClipMilliseconds(pick->durationSeconds);
            }
            if (resolved.kind == AlchemyActionKind::PutPotOnFire || resolved.kind == AlchemyActionKind::RemovePotFromFire) {
                const auto *motion = KCDC::Shared::Game::FindAlchemyKettleMotion(resolved.kind == AlchemyActionKind::PutPotOnFire);
                if (motion == nullptr) {
                    throw std::runtime_error("Missing authored kettle motion");
                }
                duration = std::max(duration, ClipMilliseconds(motion->durationSeconds));
            }
            if (resolved.kind == AlchemyActionKind::Pulse && before.contains("distillation") && before.at("distillation").at("pumps").get<std::uint32_t>() + 1 == before.at("rules").at("distillationPumps").get<std::uint32_t>()) {
                const auto *finish = KCDC::Shared::Game::FindAlchemyAnimationClip("alchemy_finish_distillation");
                if (finish == nullptr) {
                    throw std::runtime_error("Missing authored distillation completion clip");
                }
                duration += ClipMilliseconds(finish->durationSeconds);
            }
            return duration;
        }

        void ScheduleThermal(nlohmann::json &state, const nlohmann::json &before, const AlchemyAction &resolved, std::uint64_t nowMs) {
            auto &thermal     = state.at("thermal");
            const auto ids    = PotResourceIds(state.at("resources"));
            const bool onFire = state.at("onFire").get<bool>();
            if (!thermal.at("boundaries").empty()) {
                throw std::runtime_error("Unfinished alchemy thermal transition");
            }
            if (ids == thermal.at("potResourceIds") && onFire == thermal.at("onFire").get<bool>()) {
                return;
            }
            if (resolved.kind == AlchemyActionKind::PutPotOnFire || resolved.kind == AlchemyActionKind::RemovePotFromFire) {
                const auto *motion = KCDC::Shared::Game::FindAlchemyKettleMotion(onFire);
                if (motion == nullptr) {
                    throw std::runtime_error("Missing authored kettle motion");
                }
                // C_SwitchPotHalterAction's object skeleton callback
                // suppresses heating at moving, then toggles at done.
                const auto movingAt = nowMs + static_cast<std::uint64_t>(std::ceil(static_cast<double>(motion->durationSeconds) * motion->movingNormalizedTime * 1000.0));
                const auto doneAt   = nowMs + static_cast<std::uint64_t>(std::ceil(static_cast<double>(motion->durationSeconds) * motion->doneNormalizedTime * 1000.0));
                if (movingAt <= nowMs || doneAt <= movingAt || doneAt > state.at("readyAtMs").get<std::uint64_t>()) {
                    throw std::runtime_error("Invalid authored kettle event ordering");
                }
                thermal["boundaries"].push_back({{"atMs", movingAt}, {"onFire", false}, {"potResourceIds", ids}});
                thermal["boundaries"].push_back({{"atMs", doneAt}, {"onFire", onFire}, {"potResourceIds", ids}});
                return;
            }
            std::uint64_t atMs = nowMs;
            std::string event;
            if (resolved.kind == AlchemyActionKind::SelectBase) {
                event = "use";
            }
            else if (resolved.kind == AlchemyActionKind::MoveContents) {
                event = IngredientHand(static_cast<unsigned>(resolved.source)) ? "detach" : "detachresource";
            }
            else if (resolved.kind == AlchemyActionKind::FinishFromPot || resolved.kind == AlchemyActionKind::FinishFromMortar) {
                event = "detachpot";
            }
            else if (resolved.kind == AlchemyActionKind::BeginDistillation) {
                event = "detachretort";
            }
            if (!event.empty()) {
                const auto clip        = AuthoredClip(resolved, before);
                const auto *definition = KCDC::Shared::Game::FindAlchemyAnimationClip(clip);
                const auto *marker     = KCDC::Shared::Game::FindAlchemyAnimationEvent(clip, event);
                if (definition == nullptr || marker == nullptr) {
                    throw std::runtime_error("Missing unique native alchemy transfer marker");
                }
                atMs += static_cast<std::uint64_t>(std::ceil(static_cast<double>(definition->durationSeconds) * marker->normalizedTime * 1000.0));
            }
            if (atMs == nowMs) {
                thermal["onFire"]         = onFire;
                thermal["potResourceIds"] = ids;
            }
            else {
                thermal["boundaries"].push_back({{"atMs", atMs}, {"onFire", onFire}, {"potResourceIds", ids}});
            }
        }

        bool AllowedMove(KCDC::Shared::Crafting::AlchemyPosition source, KCDC::Shared::Crafting::AlchemyPosition destination) {
            return (IngredientHand(static_cast<unsigned>(source)) && (destination == KCDC::Shared::Crafting::AlchemyPosition::Bowl || destination == KCDC::Shared::Crafting::AlchemyPosition::Mortar || destination == KCDC::Shared::Crafting::AlchemyPosition::Pot))
                   || (source == KCDC::Shared::Crafting::AlchemyPosition::Bowl && (destination == KCDC::Shared::Crafting::AlchemyPosition::Mortar || destination == KCDC::Shared::Crafting::AlchemyPosition::Pot))
                   || (source == KCDC::Shared::Crafting::AlchemyPosition::Mortar && (destination == KCDC::Shared::Crafting::AlchemyPosition::Bowl || destination == KCDC::Shared::Crafting::AlchemyPosition::Pot))
                   || (source == KCDC::Shared::Crafting::AlchemyPosition::Pot && (destination == KCDC::Shared::Crafting::AlchemyPosition::Retort || destination == KCDC::Shared::Crafting::AlchemyPosition::PotionFlask));
        }

        std::size_t ContainerCount(const nlohmann::json &resources, KCDC::Shared::Crafting::AlchemyPosition container) {
            return static_cast<std::size_t>(std::count_if(resources.begin(), resources.end(), [&](const nlohmann::json &resource) {
                return InContainer(resource, container);
            }));
        }

        bool ResolveSelection(AlchemyAction &action, const nlohmann::json &state) {
            const auto held       = static_cast<KCDC::Shared::Crafting::AlchemyPosition>(state.at("held").get<unsigned>());
            const auto target     = action.destination;
            const auto &resources = state.at("resources");
            const auto pot        = ContainerCount(resources, KCDC::Shared::Crafting::AlchemyPosition::Pot);
            action.source         = held;
            if (target == KCDC::Shared::Crafting::AlchemyPosition::None) {
                if (IngredientHand(static_cast<unsigned>(held))) {
                    action.destination = held;
                    return ResolveSelection(action, state);
                }
                action.kind = AlchemyActionKind::ReturnObject;
                return action.itemId.empty()
                       && (held == KCDC::Shared::Crafting::AlchemyPosition::Bowl || held == KCDC::Shared::Crafting::AlchemyPosition::Mortar || held == KCDC::Shared::Crafting::AlchemyPosition::PotionFlask
                           || (held == KCDC::Shared::Crafting::AlchemyPosition::Bellows && !state.contains("distillation")));
            }

            if (IngredientHand(static_cast<unsigned>(held))) {
                if (target == held) {
                    action.kind         = AlchemyActionKind::ReturnIngredient;
                    const auto resource = std::find_if(resources.begin(), resources.end(), [&](const auto &row) {
                        return InContainer(row, held);
                    });
                    if (resource == resources.end()) {
                        return false;
                    }
                    action.resourceId = resource->at("id").get<std::uint32_t>();
                    return action.itemId.empty();
                }
                action.kind = AlchemyActionKind::MoveContents;
                if (held >= KCDC::Shared::Crafting::AlchemyPosition::SpecialIngredient1 && target != KCDC::Shared::Crafting::AlchemyPosition::Pot) {
                    const auto resource = std::find_if(resources.begin(), resources.end(), [&](const auto &row) {
                        return InContainer(row, held);
                    });
                    if (resource == resources.end() || !KCDC::Shared::Game::IsCrushableAlchemyIngredient(resource->at("item").get<std::string>())) {
                        return false;
                    }
                }
                return action.itemId.empty() && (target == KCDC::Shared::Crafting::AlchemyPosition::Bowl || target == KCDC::Shared::Crafting::AlchemyPosition::Mortar || (target == KCDC::Shared::Crafting::AlchemyPosition::Pot && pot != 0));
            }
            if (held == KCDC::Shared::Crafting::AlchemyPosition::Bowl || held == KCDC::Shared::Crafting::AlchemyPosition::Mortar) {
                action.kind = AlchemyActionKind::MoveContents;
                return action.itemId.empty()
                       && ((held == KCDC::Shared::Crafting::AlchemyPosition::Bowl && target == KCDC::Shared::Crafting::AlchemyPosition::Mortar) || (held == KCDC::Shared::Crafting::AlchemyPosition::Mortar && target == KCDC::Shared::Crafting::AlchemyPosition::Bowl)
                           || (target == KCDC::Shared::Crafting::AlchemyPosition::Pot && pot != 0));
            }
            if (held == KCDC::Shared::Crafting::AlchemyPosition::PotionFlask) {
                action.kind = target == KCDC::Shared::Crafting::AlchemyPosition::Retort ? AlchemyActionKind::BeginDistillation : AlchemyActionKind::FinishFromPot;
                return action.itemId.empty() && pot != 0 && (target == KCDC::Shared::Crafting::AlchemyPosition::Pot || target == KCDC::Shared::Crafting::AlchemyPosition::Retort);
            }
            if (held != KCDC::Shared::Crafting::AlchemyPosition::None) {
                return false;
            }
            if (IngredientHand(static_cast<unsigned>(target))) {
                action.kind = AlchemyActionKind::TakeIngredient;
                return !action.itemId.empty();
            }
            if (!action.itemId.empty()) {
                return false;
            }
            if (target == KCDC::Shared::Crafting::AlchemyPosition::Bellows) {
                // Factory 1808D2369 unconditionally creates Bellows with an
                // empty effective hand; it does not require a heated pot.
                action.kind = AlchemyActionKind::TakeObject;
                return true;
            }
            if (target == KCDC::Shared::Crafting::AlchemyPosition::Bowl || target == KCDC::Shared::Crafting::AlchemyPosition::PotionFlask) {
                action.kind = AlchemyActionKind::TakeObject;
                return target == KCDC::Shared::Crafting::AlchemyPosition::Bowl ? ContainerCount(resources, target) != 0 : pot >= 2;
            }
            if (target == KCDC::Shared::Crafting::AlchemyPosition::Mortar || target == KCDC::Shared::Crafting::AlchemyPosition::Pestle) {
                if (ContainerCount(resources, KCDC::Shared::Crafting::AlchemyPosition::Mortar) != 0) {
                    action.kind = AlchemyActionKind::Mill;
                    return true;
                }
                action.kind = AlchemyActionKind::FinishFromMortar;
                return pot >= 2;
            }
            if (target >= KCDC::Shared::Crafting::AlchemyPosition::Glass1 && target <= KCDC::Shared::Crafting::AlchemyPosition::Glass4) {
                // C_AlchemyResource base constructor 1806C2B08 switch 13..16.
                constexpr const char *bases[] = {"310e921d-da62-48e4-88ef-de9f295e0045", "34bd1d0b-1203-42a0-b9b4-d3585a4d9b48", "d5efb270-948b-4a38-b391-38b2edd31c8d", "e96d768e-04e5-4480-9197-1c256d642ddc"};
                action.itemId                 = bases[static_cast<unsigned>(target) - static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Glass1)];
                action.kind                   = AlchemyActionKind::SelectBase;
                return pot == 0;
            }
            if (target == KCDC::Shared::Crafting::AlchemyPosition::Pot || target == KCDC::Shared::Crafting::AlchemyPosition::PotHalter) {
                action.kind = state.at("onFire").get<bool>() ? AlchemyActionKind::RemovePotFromFire : AlchemyActionKind::PutPotOnFire;
                return true;
            }
            if (target == KCDC::Shared::Crafting::AlchemyPosition::Book || target == KCDC::Shared::Crafting::AlchemyPosition::Sandglass) {
                action.kind = AlchemyActionKind::Wait;
                return true;
            }
            return false;
        }

        /** The response's state document for a session still at the table. */
        nlohmann::json Project(const nlohmann::json &state) {
            nlohmann::json resources = nlohmann::json::array();
            for (const auto &resource : state.at("resources")) {
                resources.push_back({{"id", resource.at("id")}, {"item", resource.at("item")}, {"itemId", resource.at("itemId")}, {"position", resource.at("container")}, {"base", resource.at("base")}, {"milled", resource.at("milled")}, {"distilled", resource.at("distilled")}});
            }
            nlohmann::json result = {{"resources", std::move(resources)}, {"held", state.at("held")}, {"onFire", state.at("onFire")}, {"terminal", false}, {"pendingTerminal", false}, {"readyAtMs", state.at("readyAtMs")}, {"tableClosed", false}, {"outcome", nullptr},
                {"retiredResourceIds", nlohmann::json::array()}};
            // The owner replays the retort's pumps natively and needs the
            // count this server requires, not its own CVar.
            if (state.contains("distillation")) {
                result["distillation"] = {{"requiredPulses", state.at("rules").at("distillationPumps")}, {"completedPulses", state.at("distillation").at("pumps")}};
            }
            if (state.contains("pendingTerminal")) {
                // The staged finish owns the flask or mortar now; the native
                // table only has to animate what is about to be spent.
                result["resources"]          = nlohmann::json::array();
                result["retiredResourceIds"] = state.at("pendingTerminal").at("retiredResourceIds");
                result["terminal"]           = true;
                result["pendingTerminal"]    = true;
                result["readyAtMs"]          = state.at("pendingTerminal").at("readyAtMs");
            }
            return result;
        }

        nlohmann::json Ended(const nlohmann::json &state, const std::string &outcome, bool tableClosed) {
            return {{"resources", nlohmann::json::array()}, {"held", static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None)}, {"onFire", state.value("onFire", false)}, {"terminal", true}, {"pendingTerminal", false},
                {"readyAtMs", state.value("readyAtMs", std::uint64_t(0))}, {"tableClosed", tableClosed}, {"outcome", outcome}, {"retiredResourceIds", state.contains("resources") ? nlohmann::json(TerminalResourceIds(state.at("resources"))) : nlohmann::json::array()}};
        }

        nlohmann::json Units(const std::vector<KCDC::Features::Inventory::InventoryUnit> &units) {
            nlohmann::json result = nlohmann::json::array();
            for (const auto &unit : units) {
                const auto found = std::find_if(result.begin(), result.end(), [&](const nlohmann::json &row) {
                    return row.at("id") == unit.id;
                });
                if (found == result.end()) {
                    result.push_back({{"id", unit.id}, {"amount", unit.amount}});
                }
                else {
                    (*found)["amount"] = found->at("amount").get<std::uint32_t>() + unit.amount;
                }
            }
            return result;
        }

        const char *Phase(const nlohmann::json &state) {
            return state.contains("pendingTerminal") ? "finishing" : state.at("entryStarted").get<bool>() ? "brewing" : "entering";
        }
    } // namespace

    AlchemyReply AlchemyModule::Answer(std::uint64_t playerId, bool ok, const std::string &code) const {
        AlchemyReply reply {ok, code, "", 0, nullptr};
        if (const auto session = _sessions.find(playerId); session != _sessions.end()) {
            reply.session  = session->second.state.at("session").get<std::string>();
            reply.sequence = session->second.state.at("sequence").get<std::uint64_t>();
            reply.state    = Project(session->second.state);
        }
        else if (const auto terminal = _terminals.find(playerId); terminal != _terminals.end()) {
            reply.session  = terminal->second.session;
            reply.sequence = terminal->second.sequence;
            reply.state    = terminal->second.state;
        }
        return reply;
    }

    AlchemyReply AlchemyModule::Current(std::uint64_t playerId) const {
        return Answer(playerId, true, "");
    }

    AlchemyModule::Session *AlchemyModule::FindSession(std::uint64_t playerId, const std::string &session) {
        const auto found = _sessions.find(playerId);
        return found != _sessions.end() && found->second.state.at("session") == session ? &found->second : nullptr;
    }

    std::map<std::pair<std::string, std::uint32_t>, AlchemyModule::Table>::iterator AlchemyModule::TableOf(std::uint64_t playerId) {
        return std::find_if(_tables.begin(), _tables.end(), [playerId](const auto &entry) {
            return entry.second.playerId == playerId;
        });
    }

    std::optional<float> AlchemyModule::EntryBonus(std::uint64_t playerId) const {
        const auto table = std::find_if(_tables.begin(), _tables.end(), [playerId](const auto &entry) {
            return entry.second.playerId == playerId;
        });
        return table == _tables.end() ? std::nullopt : table->second.entryBonus;
    }

    std::string AlchemyModule::Admissible(const AlchemyActor &actor, const std::string &station, const std::string &continuationOf) const {
        if (!actor.character) {
            return "alchemyCharacterUnavailable";
        }
        const auto *placement = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(station);
        if (placement == nullptr || !Accessible(actor, *placement)) {
            return "alchemyNotAuthorized";
        }
        if (_settling.contains(actor.playerId) || _sessions.contains(actor.playerId)) {
            return "playerBusy";
        }
        const auto table = _tables.find({station, actor.virtualWorld});
        if (table != _tables.end() && table->second.playerId != actor.playerId) {
            return "stationBusy";
        }
        if (!continuationOf.empty() && (table == _tables.end() || table->second.session != continuationOf || !table->second.entryBonus)) {
            return "staleAlchemyTable";
        }
        if (actor.nowMs > std::numeric_limits<std::uint64_t>::max() - kSessionLifetimeMs) {
            return "invalidAlchemyClock";
        }
        if (KCDC::Shared::Game::FindAlchemyAnimationClip("alchemy_place_book") == nullptr) {
            return "alchemyAnimationUnavailable";
        }
        return {};
    }

    AlchemyReply AlchemyModule::Start(const AlchemyActor &actor, const std::string &request, const std::string &station, const std::string &continuationOf) {
        auto code = Admissible(actor, station, continuationOf);
        if (!code.empty()) {
            return Answer(actor.playerId, false, code);
        }
        if (_hooks.starting) {
            const nlohmann::json proposal = {{"kind", "alchemy"}, {"station", station}, {"virtualWorld", actor.virtualWorld}, {"continuationOf", continuationOf}};
            bool accepted                 = false;
            try {
                accepted = _hooks.starting(actor.playerId, proposal);
            }
            catch (...) {
            }
            if (!accepted) {
                return Answer(actor.playerId, false, "craftingStartingRejected");
            }
            // A handler may have moved the player or taken the table.
            code = Admissible(actor, station, continuationOf);
            if (!code.empty()) {
                return Answer(actor.playerId, false, code);
            }
        }
        const bool continuation = !continuationOf.empty();
        const auto key          = std::make_pair(station, actor.virtualWorld);
        std::string entryId;
        std::optional<float> entryBonus;
        if (continuation) {
            entryId    = _tables.at(key).entryId;
            entryBonus = _tables.at(key).entryBonus;
        }
        else {
            CloseTable(actor.playerId);
        }
        const auto session   = "alchemy-" + std::to_string(++_nextSession);
        const auto *opening  = KCDC::Shared::Game::FindAlchemyAnimationClip("alchemy_place_book");
        const auto readyAtMs = actor.nowMs + (continuation ? 0 : ClipMilliseconds(opening->durationSeconds));
        AlchemyRules rules;
        auto encodedRules = Rules(rules);
        RefreshRules(encodedRules, *actor.character);
        nlohmann::json state = {{"session", session}, {"startRequest", request}, {"continuationOf", continuationOf}, {"station", station}, {"level", actor.level}, {"virtualWorld", actor.virtualWorld}, {"requiredDLC", actor.requiredDLC}, {"startedMs", actor.nowMs},
            {"lastStepMs", actor.nowMs}, {"deadlineMs", actor.nowMs + kSessionLifetimeMs}, {"readyAtMs", readyAtMs}, {"sequence", 0}, {"held", static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None)}, {"nextResourceId", 1}, {"resources", nlohmann::json::array()},
            {"onFire", false}, {"heat", KCDC::Shared::Crafting::AlchemyThermal::kMinimumHeat}, {"boiling", 0U}, {"rules", encodedRules}, {"entryStarted", continuation}, {"sessionQualityBonus", entryBonus ? nlohmann::json(*entryBonus) : nlohmann::json(nullptr)}};
        state["thermal"] = {{"onFire", false}, {"potResourceIds", nlohmann::json::array()}, {"boundaries", nlohmann::json::array()}};
        _sessions[actor.playerId] = {std::move(state), *actor.character};
        _tables[key]              = {actor.playerId, continuation ? entryId : session, session, actor.nowMs + kSessionLifetimeMs, entryBonus};
        _terminals.erase(actor.playerId);
        if (continuation) {
            _events.push_back({"craftingStarted", actor.playerId, {{"kind", "alchemy"}, {"session", session}, {"station", station}, {"virtualWorld", actor.virtualWorld}, {"continuationOf", continuationOf}}});
        }
        return Answer(actor.playerId, true, "");
    }

    AlchemyReply AlchemyModule::EntryReady(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence) {
        auto *held = FindSession(actor.playerId, session);
        if (held == nullptr) {
            return Answer(actor.playerId, false, "unknownAlchemySession");
        }
        auto &state = held->state;
        if (state.at("entryStarted").get<bool>() || state.at("sequence") != 0 || sequence != 1 || !state.at("resources").empty()) {
            return Answer(actor.playerId, false, "invalidAlchemyEntryPhase");
        }
        if (!Authorized(actor, state)) {
            return Answer(actor.playerId, false, "alchemyNotAuthorized");
        }
        if (actor.nowMs >= state.at("deadlineMs").get<std::uint64_t>()) {
            return Answer(actor.playerId, false, "alchemySessionExpired");
        }
        if (actor.nowMs < state.at("readyAtMs").get<std::uint64_t>()) {
            return Answer(actor.playerId, false, "alchemyActionTooEarly");
        }
        if (!actor.character) {
            return Answer(actor.playerId, false, "alchemyCharacterUnavailable");
        }
        // Native Start stores DerivedStat 184 at C_Alchemy+6D0 once per
        // table entry; every batch of this visit keeps it.
        const float bonus = actor.character->sessionQualityBonus;
        if (!std::isfinite(bonus) || bonus < 0.0F) {
            return Answer(actor.playerId, false, "invalidAlchemyCharacter");
        }
        held->character              = *actor.character;
        state["entryStarted"]        = true;
        state["sessionQualityBonus"] = bonus;
        state["sequence"]            = sequence;
        state["lastStepMs"]          = actor.nowMs;
        state["readyAtMs"]           = actor.nowMs;
        const auto table             = _tables.find({state.at("station").get<std::string>(), state.at("virtualWorld").get<std::uint32_t>()});
        if (table != _tables.end() && table->second.playerId == actor.playerId) {
            table->second.entryBonus = bonus;
        }
        _events.push_back({"craftingStarted", actor.playerId, {{"kind", "alchemy"}, {"session", session}, {"station", state.at("station")}, {"virtualWorld", state.at("virtualWorld")}, {"continuationOf", ""}}});
        return Answer(actor.playerId, true, "");
    }

    AlchemyReply AlchemyModule::Select(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence, std::uint32_t target, const std::string &itemId) {
        if (target >= static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None)) {
            return Answer(actor.playerId, false, "invalidAlchemyTarget");
        }
        AlchemyAction action;
        action.session     = session;
        action.sequence    = sequence;
        action.kind        = AlchemyActionKind::SelectObject;
        action.itemId      = itemId;
        action.destination = static_cast<KCDC::Shared::Crafting::AlchemyPosition>(target);
        return Apply(actor, action);
    }

    AlchemyReply AlchemyModule::Return(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence) {
        AlchemyAction action;
        action.session     = session;
        action.sequence    = sequence;
        action.kind        = AlchemyActionKind::ReturnInput;
        action.destination = KCDC::Shared::Crafting::AlchemyPosition::None;
        return Apply(actor, action);
    }

    AlchemyReply AlchemyModule::Pulse(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence) {
        AlchemyAction action;
        action.session  = session;
        action.sequence = sequence;
        action.kind     = AlchemyActionKind::Pulse;
        return Apply(actor, action);
    }

    AlchemyReply AlchemyModule::Apply(const AlchemyActor &actor, const AlchemyAction &action) {
        auto *held = FindSession(actor.playerId, action.session);
        if (held == nullptr) {
            return Answer(actor.playerId, false, "unknownAlchemySession");
        }
        try {
            auto state = held->state;
            if (!state.at("entryStarted").get<bool>()) {
                return Answer(actor.playerId, false, "alchemyEntryPending");
            }
            if (!Authorized(actor, state)) {
                return Answer(actor.playerId, false, "alchemyNotAuthorized");
            }
            if (action.sequence != state.at("sequence").get<std::uint64_t>() + 1 || action.sequence > kMaximumActions) {
                return Answer(actor.playerId, false, "invalidAlchemySequence");
            }
            if (actor.nowMs < state.at("lastStepMs").get<std::uint64_t>() || actor.nowMs >= state.at("deadlineMs").get<std::uint64_t>()) {
                return Answer(actor.playerId, false, "alchemySessionExpired");
            }
            if (state.contains("pendingTerminal")) {
                return Answer(actor.playerId, false, "alchemyTerminalPending");
            }
            if (actor.nowMs < state.at("readyAtMs").get<std::uint64_t>()) {
                return Answer(actor.playerId, false, "alchemyActionTooEarly");
            }
            auto character = actor.character ? *actor.character : held->character;
            RefreshRules(state.at("rules"), character);
            Advance(state, actor.nowMs);
            const auto before = state;
            auto &resources   = state.at("resources");
            auto resolved     = action;
            if (action.kind != AlchemyActionKind::Pulse && !ResolveSelection(resolved, state)) {
                return Answer(actor.playerId, false, "invalidAlchemySelection");
            }
            const auto duration = AuthoredDuration(resolved, state);
            if (duration > state.at("deadlineMs").get<std::uint64_t>() - actor.nowMs) {
                return Answer(actor.playerId, false, "alchemySessionExpired");
            }
            state["readyAtMs"] = actor.nowMs + duration;
            // Inventory is touched only once everything else has been
            // accepted, so a refusal never leaves a unit half on the table.
            std::optional<std::string> take;
            std::optional<nlohmann::json> giveBack;
            switch (resolved.kind) {
            case AlchemyActionKind::TakeIngredient: {
                auto *inventory = _hooks.inventory ? _hooks.inventory(actor.playerId) : nullptr;
                const auto row  = inventory == nullptr ? std::map<std::string, KCDC::Features::Inventory::InventoryRow>::const_iterator() : inventory->Rows().find(resolved.itemId);
                if (resources.size() >= kMaximumResources || inventory == nullptr || row == inventory->Rows().end() || row->second.amount == 0 || !AllowedIngredient(row->second.itemClass, state.at("requiredDLC").get<std::uint16_t>())) {
                    return Answer(actor.playerId, false, "invalidAlchemyIngredient");
                }
                if (!IngredientHand(static_cast<unsigned>(resolved.destination)) || state.at("held") != static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None) || ContainerCount(resources, resolved.destination) != 0) {
                    return Answer(actor.playerId, false, "alchemyHandUnavailable");
                }
                const bool herb = KCDC::Shared::Game::FindAlchemyHerbClass(row->second.itemClass) != nullptr;
                if (herb != (resolved.destination <= KCDC::Shared::Crafting::AlchemyPosition::Herb3)) {
                    return Answer(actor.playerId, false, "invalidAlchemyIngredientSlot");
                }
                resources.push_back({{"id", state.at("nextResourceId")}, {"itemId", resolved.itemId}, {"item", row->second.itemClass}, {"metadata", row->second.metadata}, {"base", false}, {"container", static_cast<unsigned>(resolved.destination)}, {"milled", false},
                    {"distilled", false}, {"weakSeconds", 0.0F}, {"strongSeconds", 0.0F}, {"quality", IngredientQuality(row->second.itemClass, row->second.metadata, state.at("rules"))}});
                state["nextResourceId"] = state.at("nextResourceId").get<std::uint32_t>() + 1;
                state["held"]           = static_cast<unsigned>(resolved.destination);
                take                    = resolved.itemId;
                break;
            }
            case AlchemyActionKind::ReturnIngredient: {
                const auto resource = std::find_if(resources.begin(), resources.end(), [&](const nlohmann::json &row) {
                    return row.at("id") == resolved.resourceId;
                });
                if (resource == resources.end() || resource->at("base").get<bool>() || !IngredientHand(resource->at("container").get<unsigned>()) || resource->at("container") != state.at("held")
                    || ContainerCount(resources, static_cast<KCDC::Shared::Crafting::AlchemyPosition>(resource->at("container").get<unsigned>())) != 1 || resource->at("milled").get<bool>() || resource->at("distilled").get<bool>()
                    || resource->at("weakSeconds").get<float>() != 0.0F || resource->at("strongSeconds").get<float>() != 0.0F) {
                    return Answer(actor.playerId, false, "alchemyIngredientCannotReturn");
                }
                giveBack = *resource;
                resources.erase(resource);
                state["held"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None);
                break;
            }
            case AlchemyActionKind::SelectBase: {
                if (resources.size() >= kMaximumResources || std::any_of(resources.begin(), resources.end(), [](const nlohmann::json &row) {
                        return row.at("base").get<bool>();
                    })) {
                    return Answer(actor.playerId, false, "alchemyBaseAlreadyPresent");
                }
                const auto base = std::find_if(KCDC::Shared::Game::g_AlchemyBases.begin(), KCDC::Shared::Game::g_AlchemyBases.end(), [&](const auto &row) {
                    return row.item == resolved.itemId;
                });
                if (base == KCDC::Shared::Game::g_AlchemyBases.end()) {
                    return Answer(actor.playerId, false, "invalidAlchemyBase");
                }
                // A base liquid is the station's, never the player's stock.
                resources.push_back({{"id", state.at("nextResourceId")}, {"itemId", ""}, {"item", base->item}, {"metadata", nlohmann::json::object()}, {"base", true}, {"container", static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Pot)}, {"milled", false},
                    {"distilled", false}, {"weakSeconds", 0.0F}, {"strongSeconds", 0.0F}, {"quality", std::clamp(base->quality, 0.0F, 1.0F)}});
                state["nextResourceId"] = state.at("nextResourceId").get<std::uint32_t>() + 1;
                break;
            }
            case AlchemyActionKind::MoveContents: {
                if (!AllowedMove(resolved.source, resolved.destination)) {
                    return Answer(actor.playerId, false, "invalidAlchemyMove");
                }
                bool moved = false;
                for (auto &resource : resources) {
                    if (!InContainer(resource, resolved.source)) {
                        continue;
                    }
                    resource["container"] = static_cast<unsigned>(resolved.destination);
                    moved                 = true;
                }
                if (!moved) {
                    return Answer(actor.playerId, false, "alchemyContainerEmpty");
                }
                state["held"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None);
                break;
            }
            case AlchemyActionKind::Mill: {
                bool changed = false;
                for (auto &resource : resources) {
                    if (!InContainer(resource, KCDC::Shared::Crafting::AlchemyPosition::Mortar)) {
                        continue;
                    }
                    resource["milled"] = true;
                    changed            = true;
                }
                if (!changed) {
                    return Answer(actor.playerId, false, "alchemyContainerEmpty");
                }
                state["held"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Mortar);
                break;
            }
            case AlchemyActionKind::BeginDistillation:
                if (state.contains("distillation") || state.at("held") != static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::PotionFlask) || ContainerCount(resources, KCDC::Shared::Crafting::AlchemyPosition::Pot) == 0) {
                    return Answer(actor.playerId, false, "invalidAlchemyDistillation");
                }
                for (auto &resource : resources) {
                    if (InContainer(resource, KCDC::Shared::Crafting::AlchemyPosition::Pot)) {
                        resource["container"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Retort);
                    }
                }
                state["held"]         = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Bellows);
                state["distillation"] = {{"pumps", 0}};
                break;
            case AlchemyActionKind::Pulse:
                if (state.at("held") != static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::Bellows)) {
                    return Answer(actor.playerId, false, "alchemyBellowsNotHeld");
                }
                state["heat"] = KCDC::Shared::Crafting::AlchemyThermal::kMaximumHeat;
                if (state.contains("distillation")) {
                    auto pumps          = state.at("distillation").at("pumps").get<std::uint32_t>();
                    const auto required = state.at("rules").at("distillationPumps").get<std::uint32_t>();
                    if (pumps >= required || ContainerCount(resources, KCDC::Shared::Crafting::AlchemyPosition::Retort) == 0) {
                        return Answer(actor.playerId, false, "invalidAlchemyDistillation");
                    }
                    state["distillation"]["pumps"] = ++pumps;
                    if (pumps == required) {
                        for (auto &resource : resources) {
                            if (InContainer(resource, KCDC::Shared::Crafting::AlchemyPosition::Retort)) {
                                resource["container"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::PotionFlask);
                                resource["distilled"] = true;
                            }
                        }
                        state["finishContainer"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::PotionFlask);
                        state["pendingTerminal"] = {{"readyAtMs", state.at("readyAtMs")}, {"retiredResourceIds", TerminalResourceIds(resources)}};
                    }
                }
                break;
            case AlchemyActionKind::PutPotOnFire: state["onFire"] = true; break;
            case AlchemyActionKind::RemovePotFromFire: state["onFire"] = false; break;
            case AlchemyActionKind::Wait: break;
            case AlchemyActionKind::ReturnObject: state["held"] = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None); break;
            case AlchemyActionKind::TakeObject: state["held"] = static_cast<unsigned>(resolved.destination); break;
            case AlchemyActionKind::FinishFromPot:
            case AlchemyActionKind::FinishFromMortar: {
                const bool mortar      = resolved.kind == AlchemyActionKind::FinishFromMortar;
                const auto destination = mortar ? KCDC::Shared::Crafting::AlchemyPosition::Mortar : KCDC::Shared::Crafting::AlchemyPosition::PotionFlask;
                for (auto &resource : resources) {
                    if (!InContainer(resource, KCDC::Shared::Crafting::AlchemyPosition::Pot)) {
                        continue;
                    }
                    resource["container"] = static_cast<unsigned>(destination);
                    if (mortar) {
                        resource["milled"] = true;
                    }
                }
                // Staged, never settled here: Complete grants after the
                // authored finish animation has played.
                state["finishContainer"] = static_cast<unsigned>(destination);
                state["pendingTerminal"] = {{"readyAtMs", state.at("readyAtMs")}, {"retiredResourceIds", TerminalResourceIds(resources)}};
                break;
            }
            default: return Answer(actor.playerId, false, "unsupportedAlchemyAction");
            }
            ScheduleThermal(state, before, resolved, actor.nowMs);
            state["sequence"] = action.sequence;
            if (take || giveBack) {
                auto *inventory = _hooks.inventory ? _hooks.inventory(actor.playerId) : nullptr;
                if (inventory == nullptr) {
                    return Answer(actor.playerId, false, "inventoryUnavailable");
                }
                if (take) {
                    const auto removed = inventory->Remove({{*take, 1}}, std::nullopt, kReason);
                    if (!removed.ok) {
                        return Answer(actor.playerId, false, "invalidAlchemyIngredient");
                    }
                }
                if (giveBack) {
                    const auto added = inventory->Add(giveBack->at("item").get<std::string>(), 1, giveBack->at("metadata"), std::nullopt, kReason);
                    if (!added.ok) {
                        return Answer(actor.playerId, false, added.code);
                    }
                }
            }
            held->state     = std::move(state);
            held->character = std::move(character);
            return Answer(actor.playerId, true, "");
        }
        catch (const std::exception &) {
            return Answer(actor.playerId, false, "invalidAlchemyState");
        }
    }

    AlchemyReply AlchemyModule::Complete(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence) {
        auto *held = FindSession(actor.playerId, session);
        if (held == nullptr) {
            return Answer(actor.playerId, false, "unknownAlchemySession");
        }
        try {
            auto state = held->state;
            if (!state.contains("pendingTerminal") || sequence != state.at("sequence").get<std::uint64_t>()) {
                return Answer(actor.playerId, false, "invalidAlchemyTerminal");
            }
            if (!Authorized(actor, state)) {
                return Answer(actor.playerId, false, "alchemyNotAuthorized");
            }
            if (actor.nowMs < state.at("pendingTerminal").at("readyAtMs").get<std::uint64_t>()) {
                return Answer(actor.playerId, false, "alchemyActionTooEarly");
            }
            if (actor.nowMs >= state.at("deadlineMs").get<std::uint64_t>()) {
                return Answer(actor.playerId, false, "alchemySessionExpired");
            }
            if (actor.character) {
                held->character = *actor.character;
            }
            RefreshRules(state.at("rules"), held->character);
            Advance(state, actor.nowMs);
            return Finish(actor.playerId, *held, std::move(state));
        }
        catch (const std::exception &) {
            return Answer(actor.playerId, false, "invalidAlchemyState");
        }
    }

    AlchemyReply AlchemyModule::Finish(std::uint64_t playerId, Session &held, nlohmann::json state) {
        const auto &character = held.character;
        std::vector<AlchemyResource> resources;
        float ingredientQuality = 0.0F, baseQuality = 1.0F;
        std::size_t ingredientCount = 0;
        for (const auto &resource : state.at("resources")) {
            if (resource.at("container") != state.at("finishContainer")) {
                continue;
            }
            const bool base = resource.at("base").get<bool>();
            resources.push_back({resource.at("item").get<std::string>(), base, resource.at("milled").get<bool>(), resource.at("distilled").get<bool>(), resource.at("weakSeconds").get<float>(), resource.at("strongSeconds").get<float>()});
            if (base) {
                baseQuality = resource.at("quality").get<float>();
            }
            else {
                ingredientQuality += resource.at("quality").get<float>();
                ++ingredientCount;
            }
        }
        if (resources.empty()) {
            return Answer(playerId, false, "alchemyContainerEmpty");
        }
        const auto &rules = state.at("rules");
        const auto match  = AlchemyEvaluator::Match(resources, rules.at("boilingTolerance").get<float>(), rules.at("timeUnitSeconds").get<float>(), state.at("requiredDLC").get<std::uint16_t>());
        if (!match.valid) {
            return Answer(playerId, false, "invalidAlchemyState");
        }
        if (match.recipe != nullptr && !match.contentAllowed) {
            return Answer(playerId, false, "contentUnavailable");
        }
        struct Output {
            std::string itemClass;
            std::uint32_t amount = 0;
            nlohmann::json metadata;
        };
        std::optional<Output> output;
        float xp            = rules.at("failureXP").get<float>();
        float quality       = 0.0F;
        bool success        = false;
        std::uint32_t grade = 1;
        if (match.recipe != nullptr) {
            if (ingredientCount == 0 || match.recipe->products.empty()) {
                return Answer(playerId, false, "invalidAlchemyRecipe");
            }
            const auto allowed         = rules.at("allowedMistakes").get<std::uint32_t>();
            const auto correct         = static_cast<std::uint32_t>(match.successfulSteps.size());
            success                    = correct + allowed >= match.totalSteps;
            const float baseWeight     = rules.at("baseQualityWeight").get<float>();
            const float resourceWeight = rules.at("resourceQualityWeight").get<float>();
            const float stepQuality    = std::clamp((static_cast<float>(correct) + static_cast<float>(allowed) - static_cast<float>(match.totalSteps)) / static_cast<float>(allowed), 0.0F, 1.0F);
            quality                    = baseWeight * baseQuality + resourceWeight * ingredientQuality / static_cast<float>(ingredientCount) + (1.0F - baseWeight - resourceWeight) * stepQuality;
            std::string error;
            if (!AlchemyCharacterEvaluator::ApplyScore(character, quality, quality, error)) {
                return Answer(playerId, false, error);
            }
            // Native Start stores DerivedStat 184 at C_Alchemy+6D0;
            // EvaluateContainer adds that entry-time value after 74.
            quality = std::clamp(quality + state.at("sessionQualityBonus").get<float>(), 0.0F, 1.0F);
            std::vector<const KCDC::Shared::Game::CraftProduct *> products;
            for (const auto &product : match.recipe->products) {
                products.push_back(&product);
            }
            std::sort(products.begin(), products.end(), [](const auto *a, const auto *b) {
                return a->threshold < b->threshold;
            });
            std::size_t index = 0;
            if (products.size() == 4) {
                index = quality >= rules.at("firstQualityThreshold").get<float>() ? 2 : quality >= rules.at("secondQualityThreshold").get<float>() ? 1 : 0;
            }
            else if (products.size() > 1) {
                index = static_cast<std::size_t>(std::round(quality * static_cast<float>(products.size() - 2)));
            }
            if (products.size() > 1 && character.secretOfSecrets) {
                index = std::min(index + 1, products.size() - 1);
            }
            const auto *product = products.at(index);
            if (product->maxYield == 0 || product->maxYield > kMaximumProductYield) {
                return Answer(playerId, false, "invalidAlchemyYield");
            }
            grade  = product->threshold;
            output = Output {success ? std::string(product->item) : std::string(kFailedPotion), product->maxYield, nlohmann::json::object()};
            float multiplier                   = 1.0F;
            const std::int64_t experienceGrade = static_cast<std::int64_t>(grade) - (character.secretOfSecrets ? 1 : 0);
            if (experienceGrade == 2) {
                multiplier = rules.at("secondQualityXPMultiplier").get<float>();
            }
            else if (experienceGrade == 3) {
                multiplier = rules.at("thirdQualityXPMultiplier").get<float>();
            }
            xp = success ? static_cast<float>(match.baseXP) * multiplier + static_cast<float>(match.stepXP) : rules.at("failureXP").get<float>();
        }
        else {
            // 182E16680 initializes descriptor {recipe0, passed0, total10,
            // baseXP0, stepXP0, grade1, yield1}; slot 13 substitutes generic.
            output = Output {kFailedPotion, 1, nlohmann::json::object()};
        }
        // RPG result 181FD43E0 applies Bundle 30 before modifier 73 and
        // floors its output, even for the generic failed potion.
        const auto *missile = KCDC::Shared::Game::FindMissileClass(KCDC::Shared::Game::ItemClassIdFromGuid(output->itemClass));
        if (character.experimentalPowder && missile != nullptr && !missile->weapon) {
            output->metadata["onEquipBuffs"] = nlohmann::json::array({KCDC::Shared::Inventory::kExperimentalPowderItemBuff});
        }
        float amount = static_cast<float>(output->amount);
        if (character.bundleAlchemist) {
            amount += static_cast<float>(character.bundleExtraYield);
        }
        std::string error;
        if (!AlchemyCharacterEvaluator::ApplyYield(character, amount, amount, error)) {
            return Answer(playerId, false, error);
        }
        if (!std::isfinite(amount) || amount < 0.0F || std::floor(amount) > kMaximumProductYield) {
            return Answer(playerId, false, "invalidAlchemyYield");
        }
        output->amount = static_cast<std::uint32_t>(std::floor(amount));
        if (output->amount == 0) {
            output.reset();
        }
        // Native 180487920 converts positive XP to 1/128 units by
        // int(xp*128+0.5), not an unrounded floating point addition.
        constexpr float kNativeXPScale = static_cast<float>(AlchemyCharacterEvaluator::kExperienceUnitsPerPoint);
        xp                             = static_cast<float>(static_cast<std::int64_t>(xp * kNativeXPScale + 0.5F)) / kNativeXPScale;
        const std::string recipe       = match.recipe != nullptr ? std::string(match.recipe->id) : std::string();
        const std::string outcome      = success ? "success" : "failed";
        const auto session             = state.at("session").get<std::string>();
        const auto sequence            = state.at("sequence").get<std::uint64_t>();
        const nlohmann::json proposal  = {{"kind", "alchemy"}, {"session", session}, {"station", state.at("station")}, {"outcome", outcome}, {"recipe", recipe}, {"grade", grade}, {"product", output ? output->itemClass : std::string()}, {"amount", output ? output->amount : 0U},
            {"quality", quality}, {"xp", xp}};
        bool accepted = true;
        if (_hooks.completing) {
            try {
                accepted = _hooks.completing(playerId, proposal);
            }
            catch (...) {
                accepted = false;
            }
        }
        // The handler may have ended or replaced this session.
        auto *current = FindSession(playerId, session);
        if (current == nullptr || !current->state.contains("pendingTerminal") || current->state.at("sequence") != sequence) {
            return Answer(playerId, false, "alchemyCompletionSuperseded");
        }
        if (!accepted) {
            // What was spent stays spent; nothing is granted.
            Retire(playerId, "failed", "craftingCompletingRejected", true, sequence);
            return Answer(playerId, false, "craftingCompletingRejected");
        }
        nlohmann::json outputs = nlohmann::json::array();
        if (output) {
            auto *inventory = _hooks.inventory ? _hooks.inventory(playerId) : nullptr;
            if (inventory == nullptr) {
                return Answer(playerId, false, "inventoryUnavailable");
            }
            const auto added = inventory->Add(output->itemClass, output->amount, output->metadata, std::nullopt, kReason, true);
            if (!added.ok) {
                return Answer(playerId, false, added.code);
            }
            outputs = Units(added.outputs);
        }
        current->state = std::move(state);
        if (xp > 0.0F && _hooks.experience) {
            _hooks.experience(playerId, static_cast<double>(xp));
        }
        if (success && match.recipe != nullptr) {
            // Completion slot 8 181326AAC calls full-unlock 181326B0C, which
            // ORs all 29 knowledge bits, not just the passed recipe steps.
            _knowledge[playerId][recipe] |= kRecipeKnowledgeMask;
        }
        nlohmann::json completed = {{"kind", "alchemy"}, {"session", session}, {"station", current->state.at("station")}, {"virtualWorld", current->state.at("virtualWorld")}, {"outcome", outcome}, {"recipe", recipe}, {"grade", grade}, {"quality", quality}, {"xp", xp}};
        completed["outputs"]     = std::move(outputs);
        completed["knowledge"]   = Knowledge(playerId);
        _events.push_back({"craftingCompleted", playerId, std::move(completed)});
        Retire(playerId, outcome, "", false, sequence);
        return Answer(playerId, true, "");
    }

    nlohmann::json AlchemyModule::Refund(std::uint64_t playerId, const nlohmann::json &resource) {
        auto *inventory = _hooks.inventory ? _hooks.inventory(playerId) : nullptr;
        if (inventory == nullptr) {
            return nlohmann::json::array();
        }
        const auto added = inventory->Add(resource.at("item").get<std::string>(), 1, resource.at("metadata"), std::nullopt, kReason);
        return added.ok ? Units(added.outputs) : nlohmann::json::array();
    }

    void AlchemyModule::Retire(std::uint64_t playerId, const std::string &outcome, const std::string &reason, bool closeTable, std::uint64_t sequence) {
        const auto found = _sessions.find(playerId);
        if (found == _sessions.end()) {
            return;
        }
        // Out of the map before anything reaches the inventory.
        const auto state = std::move(found->second.state);
        _sessions.erase(found);
        std::vector<KCDC::Features::Inventory::InventoryUnit> refunded;
        const auto &resources = state.at("resources");
        // Held until the terminal event, also when an inventory write throws:
        // a leaked entry would refuse the player's every later table and cancel.
        struct SettlingScope {
            std::set<std::uint64_t> &players;
            std::uint64_t playerId;
            ~SettlingScope() {
                players.erase(playerId);
            }
        } settling {_settling, playerId};
        _settling.insert(playerId);
        bool fullRefund = false;
        if (closeTable && _hooks.refunding) {
            nlohmann::json materials = nlohmann::json::array();
            for (const auto &resource : resources) {
                if (!resource.at("base").get<bool>()) {
                    materials.push_back({{"item", resource.at("item")}, {"amount", 1}, {"metadata", resource.at("metadata")}});
                }
            }
            const nlohmann::json proposal = {{"kind", "alchemy"}, {"session", state.at("session")}, {"station", state.at("station")}, {"virtualWorld", state.at("virtualWorld")}, {"outcome", outcome}, {"reason", reason}, {"materials", std::move(materials)}};
            try {
                fullRefund = _hooks.refunding(playerId, proposal);
            }
            catch (...) {
                // A failed policy leaves the native settlement in force.
            }
        }
        for (const auto &resource : resources) {
            if (resource.at("base").get<bool>() || (!fullRefund && !RefundableContainer(resources, resource.at("container").get<unsigned>()))) {
                continue;
            }
            for (const auto &unit : Refund(playerId, resource)) {
                refunded.push_back({unit.at("id").get<std::string>(), unit.at("amount").get<std::uint32_t>()});
            }
        }
        const auto session = state.at("session").get<std::string>();
        const auto table   = _tables.find({state.at("station").get<std::string>(), state.at("virtualWorld").get<std::uint32_t>()});
        const bool ours    = table != _tables.end() && table->second.playerId == playerId && table->second.session == session;
        if (ours && closeTable) {
            _tables.erase(table);
        }
        _terminals[playerId] = {session, sequence, Ended(state, outcome, closeTable || !ours)};
        _events.push_back({"craftingEnded", playerId, {{"kind", "alchemy"}, {"session", session}, {"station", state.at("station")}, {"virtualWorld", state.at("virtualWorld")}, {"outcome", outcome}, {"reason", reason}, {"refunded", Units(refunded)}}});
    }

    void AlchemyModule::CloseTable(std::uint64_t playerId) {
        const auto table = TableOf(playerId);
        if (table == _tables.end()) {
            return;
        }
        const auto session = table->second.session;
        _tables.erase(table);
        auto &terminal = _terminals[playerId];
        if (terminal.session != session || !terminal.state.is_object()) {
            terminal = {session, 0, Ended(nlohmann::json::object(), "cancelled", true)};
        }
        terminal.state["tableClosed"] = true;
    }

    AlchemyReply AlchemyModule::Cancel(const AlchemyActor &actor, const std::string &session, const std::string &startRequest, const std::string &continuationOf, const std::string &reason) {
        const auto playerId = actor.playerId;
        if (_settling.contains(playerId)) {
            return Answer(playerId, false, "sessionNotOwned");
        }
        if (const auto active = _sessions.find(playerId); active != _sessions.end()) {
            const auto &state = active->second.state;
            if ((session.empty() || state.at("session") != session) && (startRequest.empty() || state.at("startRequest") != startRequest)) {
                return Answer(playerId, false, "sessionNotOwned");
            }
            // A client report can arrive before Expire in this tick. Server
            // closure reasons still take precedence over its refund request.
            if (actor.nowMs >= state.at("deadlineMs").get<std::uint64_t>()) {
                End(playerId, "timeout");
                return Answer(playerId, false, "alchemySessionExpired");
            }
            if (!Authorized(actor, state)) {
                End(playerId, "contextInvalidated");
                return Answer(playerId, false, "alchemyNotAuthorized");
            }
            Retire(playerId, "cancelled", reason, true, state.at("sequence").get<std::uint64_t>() + 1);
            return Answer(playerId, true, "");
        }
        // No batch is running: close the table this player kept between
        // batches, when it is the one named. Nothing named is already over.
        const auto table    = TableOf(playerId);
        const auto &kept    = continuationOf.empty() ? session : continuationOf;
        if (table != _tables.end() && !kept.empty() && table->second.session == kept) {
            CloseTable(playerId);
        }
        return Answer(playerId, true, "");
    }

    bool AlchemyModule::End(std::uint64_t playerId, const std::string &reason) {
        if (_settling.contains(playerId)) {
            return false;
        }
        if (const auto active = _sessions.find(playerId); active != _sessions.end()) {
            Retire(playerId, "cancelled", reason, true, active->second.state.at("sequence").get<std::uint64_t>() + 1);
            return true;
        }
        if (TableOf(playerId) == _tables.end()) {
            return false;
        }
        CloseTable(playerId);
        return true;
    }

    std::vector<std::pair<std::uint64_t, std::string>> AlchemyModule::Expire(std::uint64_t nowMs, const fu2::function<std::optional<AlchemyActor>(std::uint64_t playerId) const> &locate) {
        std::vector<std::pair<std::uint64_t, std::string>> closing;
        for (const auto &[playerId, held] : _sessions) {
            const auto actor = locate(playerId);
            if (nowMs >= held.state.at("deadlineMs").get<std::uint64_t>()) {
                closing.emplace_back(playerId, "timeout");
            }
            else if (!actor || !Authorized(*actor, held.state)) {
                closing.emplace_back(playerId, "contextInvalidated");
            }
        }
        for (const auto &[key, table] : _tables) {
            if (_sessions.contains(table.playerId)) {
                continue;
            }
            const auto actor    = locate(table.playerId);
            const auto *station = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(key.first);
            if (nowMs >= table.deadlineMs) {
                closing.emplace_back(table.playerId, "timeout");
            }
            else if (!actor || station == nullptr || !Accessible(*actor, *station) || actor->virtualWorld != key.second) {
                closing.emplace_back(table.playerId, "contextInvalidated");
            }
        }
        for (const auto &[playerId, reason] : closing) {
            End(playerId, reason);
        }
        return closing;
    }

    void AlchemyModule::Forget(std::uint64_t playerId) {
        End(playerId, "disconnected");
        _terminals.erase(playerId);
        _knowledge.erase(playerId);
    }

    nlohmann::json AlchemyModule::Inspect(std::uint64_t playerId, std::uint64_t nowMs) const {
        const auto remaining = [nowMs](std::uint64_t deadline) {
            return deadline > nowMs ? deadline - nowMs : 0;
        };
        if (const auto active = _sessions.find(playerId); active != _sessions.end()) {
            const auto &state = active->second.state;
            return {{"id", state.at("session")}, {"station", state.at("station")}, {"virtualWorld", state.at("virtualWorld")}, {"phase", Phase(state)}, {"sequence", state.at("sequence")}, {"resources", Project(state).at("resources")},
                {"expiresInMs", remaining(state.at("deadlineMs").get<std::uint64_t>())}};
        }
        for (const auto &[key, table] : _tables) {
            if (table.playerId != playerId) {
                continue;
            }
            const auto terminal = _terminals.find(playerId);
            const auto sequence = terminal == _terminals.end() ? std::uint64_t(0) : terminal->second.sequence;
            return {{"id", table.session}, {"station", key.first}, {"virtualWorld", key.second}, {"phase", "betweenBatches"}, {"sequence", sequence}, {"resources", nlohmann::json::array()}, {"expiresInMs", remaining(table.deadlineMs)}};
        }
        return nullptr;
    }

    nlohmann::json AlchemyModule::Occupancy(const std::string &station, std::uint32_t virtualWorld, std::uint64_t nowMs) const {
        const auto *placement = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(station);
        if (placement == nullptr || placement->kind != KCDC::Shared::Game::CraftKind::Alchemy) {
            return nullptr;
        }
        nlohmann::json result = {{"station", station}, {"virtualWorld", virtualWorld}, {"occupied", false}, {"occupant", nullptr}};
        const auto table      = _tables.find({station, virtualWorld});
        if (table == _tables.end()) {
            return result;
        }
        const auto active   = _sessions.find(table->second.playerId);
        const char *phase   = active == _sessions.end() ? "betweenBatches" : Phase(active->second.state);
        result["occupied"]  = true;
        result["occupant"]  = {{"playerId", table->second.playerId}, {"entryId", table->second.entryId}, {"sessionId", table->second.session}, {"phase", phase}, {"expiresInMs", table->second.deadlineMs > nowMs ? table->second.deadlineMs - nowMs : 0}};
        return result;
    }

    nlohmann::json AlchemyModule::Knowledge(std::uint64_t playerId) const {
        nlohmann::json result = nlohmann::json::object();
        if (const auto found = _knowledge.find(playerId); found != _knowledge.end()) {
            for (const auto &[recipe, mask] : found->second) {
                result[recipe] = mask;
            }
        }
        return result;
    }

    bool AlchemyModule::SetKnowledge(std::uint64_t playerId, const std::string &recipe, std::uint32_t mask) {
        if (mask > kRecipeKnowledgeMask || KCDC::Shared::Game::CustomRecipeRegistry::Get().Find(KCDC::Shared::Game::CraftKind::Alchemy, recipe) == nullptr) {
            return false;
        }
        if (mask == 0) {
            if (const auto found = _knowledge.find(playerId); found != _knowledge.end()) {
                found->second.erase(recipe);
            }
        }
        else {
            _knowledge[playerId][recipe] = mask;
        }
        return true;
    }

    std::vector<AlchemyEvent> AlchemyModule::TakeEvents() {
        std::vector<AlchemyEvent> events;
        events.swap(_events);
        return events;
    }
} // namespace KCDC::Features::Crafting
