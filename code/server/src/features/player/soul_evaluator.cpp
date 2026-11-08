#include "soul_evaluator.h"
#include "shared/features/crafting/alchemy_native_ids.h"
#include "shared/features/crafting/smithing_native_ids.h"

#include "shared/game/buff_catalog.h"
#include "shared/game/buff_rule_catalog.h"
#include "shared/game/soul_profile_catalog.h"
#include "shared/game/soul_rule_catalog.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>

namespace KCDC::Features::Player {
    bool EvaluateSoulDailyWindow(std::uint64_t worldMilliseconds, float startHour, float endHour, bool &active, std::string &error) {
        constexpr float kMillisecondsPerHour = 3600000.0F;
        constexpr float kHoursPerDay         = 24.0F;
        if (worldMilliseconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || !std::isfinite(startHour) || !std::isfinite(endHour)) {
            error = "invalidNativeCalendarWindow";
            return false;
        }
        // 1805356AC -> fmodf -> 1805356E8 -> 1805352E0. Preserve both
        // float round trips and the integer truncation, including large days.
        const float totalHours     = static_cast<float>(static_cast<std::int64_t>(worldMilliseconds)) / kMillisecondsPerHour;
        const float dayHours       = std::fmod(totalHours, kHoursPerDay);
        const auto dayMilliseconds = static_cast<std::int64_t>(dayHours * kMillisecondsPerHour);
        const float hour           = static_cast<float>(dayMilliseconds) / kMillisecondsPerHour;
        active                     = endHour > startHour ? hour >= startHour && hour <= endHour : hour >= startHour || hour <= endHour;
        error.clear();
        return true;
    }

    bool ResolveCanonicalSoulTerms(const SoulSnapshot &snapshot, const SoulClock &clock, KCDC::Shared::Game::SoulScalarDomain domain, std::uint32_t selector, std::vector<KCDC::Shared::Game::SoulScalarTerm> &terms, std::string &error, SoulEvaluationScope scope) {
        terms.clear();
        const bool alchemyOutcome        = scope == SoulEvaluationScope::AlchemyOutcome;
        const bool smithingOutcome       = scope == SoulEvaluationScope::SmithingOutcome;
        const bool outcomeScope          = alchemyOutcome || smithingOutcome;
        const bool supportedAlchemyQuery = (domain == KCDC::Shared::Game::SoulScalarDomain::Skill && selector == KCDC::Shared::Crafting::AlchemyNative::kSkill)
                                           || (domain == KCDC::Shared::Game::SoulScalarDomain::Adjustment && (selector == KCDC::Shared::Crafting::AlchemyNative::kYieldModifier || selector == KCDC::Shared::Crafting::AlchemyNative::kScoreModifier))
                                           || (domain == KCDC::Shared::Game::SoulScalarDomain::DerivedStat && (selector == KCDC::Shared::Game::SoulNative::kExperienceMultiplierStat || selector == KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusStat));
        const bool supportedSmithingQuery =
            (domain == KCDC::Shared::Game::SoulScalarDomain::Skill && selector == KCDC::Shared::Crafting::SmithingNative::kSkill) || (domain == KCDC::Shared::Game::SoulScalarDomain::DerivedStat && selector == KCDC::Shared::Crafting::SmithingNative::kMaximumLockpicksStat);
        if (scope > SoulEvaluationScope::SmithingOutcome || (alchemyOutcome && !supportedAlchemyQuery) || (smithingOutcome && !supportedSmithingQuery)) {
            error = "unsupportedSoulEvaluationScope";
            return false;
        }
        if (clock.realMilliseconds < snapshot.clock.realMilliseconds || clock.worldMilliseconds < snapshot.clock.worldMilliseconds) {
            error = "soulClockDiscontinuity";
            return false;
        }
        std::string_view token;
        if (domain == KCDC::Shared::Game::SoulScalarDomain::Skill || domain == KCDC::Shared::Game::SoulScalarDomain::Stat) {
            const auto &definitions = domain == KCDC::Shared::Game::SoulScalarDomain::Skill ? KCDC::Shared::Game::g_SoulSkills : KCDC::Shared::Game::g_SoulStats;
            for (const auto &definition : definitions) {
                if (definition.id == selector) {
                    token = definition.name;
                }
            }
        }
        else if (domain == KCDC::Shared::Game::SoulScalarDomain::Adjustment) {
            if (selector == KCDC::Shared::Crafting::AlchemyNative::kYieldModifier) {
                token = "ala";
            }
            else if (selector == KCDC::Shared::Crafting::AlchemyNative::kScoreModifier) {
                token = "apq";
            }
            else if (selector == KCDC::Shared::Game::SoulNative::kPotionUpdateAdjustment) {
                token = "nhp";
            }
        }
        else if (domain == KCDC::Shared::Game::SoulScalarDomain::DerivedStat) {
            if (selector == KCDC::Shared::Game::SoulNative::kExperienceMultiplierStat) {
                token = "xpm";
            }
            else if (selector == KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusStat) {
                token = "bap";
            }
            else if (selector == KCDC::Shared::Crafting::SmithingNative::kMaximumLockpicksStat) {
                token = "lfb";
            }
        }
        if (token.empty()) {
            error = "unsupportedCanonicalSoulQuery";
            return false;
        }

        struct Source {
            std::string id;
            std::string definition;
            bool requiresSimulation;
        };
        std::vector<Source> sources;
        for (const auto &effect : snapshot.profile.effects) {
            const bool requiresSimulation = effect.expiresAt.has_value();
            if (!outcomeScope && requiresSimulation) {
                error = "activeEffectSimulationRequired:" + effect.id;
                return false;
            }
            sources.push_back({effect.id, effect.definition, requiresSimulation});
        }
        for (const auto &edge : KCDC::Shared::Game::g_PerkBuffRules) {
            if (snapshot.profile.perks.contains(std::string(edge.perk))) {
                sources.push_back({std::string(edge.perk), std::string(edge.buff), false});
            }
        }
        for (const auto &remap : KCDC::Shared::Game::g_PerkBuffRemaps) {
            if (snapshot.profile.perks.contains(std::string(remap.perk)) && std::any_of(sources.begin(), sources.end(), [&remap](const auto &source) {
                    return source.definition == remap.source;
                })) {
                error = "activeEffectRemapRequiresEvaluator:" + std::string(remap.perk);
                return false;
            }
        }
        std::set<std::string> definitions;
        std::vector<KCDC::Shared::Game::SoulScalarTerm> resolved;
        for (const auto &[source, id, requiresSimulation] : sources) {
            const auto *rule       = KCDC::Shared::Game::FindBuffRule(id);
            const auto *definition = KCDC::Shared::Game::FindBuffByGuid(id);
            const bool described   = rule != nullptr && definition != nullptr && rule->parametersAuthored;
            std::string parameters;
            if (described) {
                for (char c : rule->parameters) {
                    if (c != ' ') {
                        parameters.push_back(c);
                    }
                }
            }
            bool contributes = false;
            for (std::size_t begin = 0; begin < parameters.size();) {
                const auto end = parameters.find_first_of(",;", begin);
                const std::string_view expression(parameters.data() + begin, (end == std::string::npos ? parameters.size() : end) - begin);
                if (expression.substr(0, expression.find_first_of("+-*%<>!=")) == token) {
                    contributes = true;
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
            // Brewing reads a handful of selectors, and the buffs a live
            // player reports are many: a source whose authored parameters
            // name none of them has no bearing on the outcome. One the
            // catalog cannot describe names none either.
            if (outcomeScope && !contributes) {
                continue;
            }
            // perk_swordmakers_wisdom: "count as 3 levels higher during
            // blacksmithing". A smithing query is always asked during it.
            const bool blacksmithing = smithingOutcome && described && rule->implementation == "Cpp:Blacksmithing";
            if (!described || (rule->implementation != "Cpp:Constant" && rule->implementation != "Cpp:BlackArtsApprentice" && !blacksmithing)) {
                if (smithingOutcome) {
                    continue;
                }
                error = "unsupportedActiveEffect:" + source + ":" + id;
                return false;
            }
            if (requiresSimulation || definition->duration >= 0.0F) {
                if (smithingOutcome) {
                    continue;
                }
                error = "activeEffectSimulationRequired:" + source;
                return false;
            }
            // Native exclusivity/stack replacement has not yet been projected
            // for duplicate definitions. Do not count a possible replacement twice.
            if (!definitions.insert(id).second) {
                error = "ambiguousActiveEffectStack:" + id;
                return false;
            }
            bool enabled = true;
            if (rule->implementation == "Cpp:BlackArtsApprentice") {
                std::optional<float> start;
                std::optional<float> end;
                for (const auto &parameter : KCDC::Shared::Game::g_SoulRuleParameters) {
                    if (parameter.name == "BlackArtsApprenticeBuffStartTime") {
                        start = parameter.value;
                    }
                    if (parameter.name == "BlackArtsApprenticeBuffEndTime") {
                        end = parameter.value;
                    }
                }
                if (!start || !end) {
                    error = "missingBlackArtsNativeRules";
                    return false;
                }
                if (!EvaluateSoulDailyWindow(clock.worldMilliseconds, *start, *end, enabled, error)) {
                    return false;
                }
            }
            // Permanent Constant implementations may still change resources
            // every tick. Absence of a finite duration is not statelessness.
            if (!outcomeScope && parameters.find("/s") != std::string::npos) {
                error = "activeEffectSimulationRequired:" + source;
                return false;
            }
            std::size_t begin = 0;
            while (begin < parameters.size()) {
                const auto end = parameters.find_first_of(",;", begin);
                const std::string_view expression(parameters.data() + begin, (end == std::string::npos ? parameters.size() : end) - begin);
                const auto operation = expression.find_first_of("+-*%<>!=");
                const auto name      = expression.substr(0, operation);
                if (name == token) {
                    if (operation == std::string_view::npos || parameters.find(';') != std::string::npos) {
                        error = "unsupportedContributingExpression:" + source;
                        return false;
                    }
                    constexpr std::string_view operators = "+-*%<>!=";
                    const auto code                      = operators.find(expression[operation]);
                    float value                          = 0.0F;
                    const auto text                      = expression.substr(operation + 1);
                    const auto parsed                    = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
                        error = "unsupportedContributingValue:" + source;
                        return false;
                    }
                    resolved.push_back({domain, selector, static_cast<KCDC::Shared::Game::SoulModifierOperation>(code), value, enabled, definition->lifetime == 0, false, std::nullopt, source});
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
        }
        // 0x18063B7C8 inserts by ascending operation. Ties preserve native
        // instance-pointer traversal. Distinct tied values cannot be sorted
        // by server identity while claiming native float-order equivalence.
        std::stable_sort(resolved.begin(), resolved.end(), [](const KCDC::Shared::Game::SoulScalarTerm &a, const KCDC::Shared::Game::SoulScalarTerm &b) {
            return *a.operation < *b.operation;
        });
        const KCDC::Shared::Game::SoulScalarTerm *previousEnabled = nullptr;
        for (const auto &current : resolved) {
            if (!current.enabled) {
                continue;
            }
            if (previousEnabled != nullptr && previousEnabled->operation == current.operation && previousEnabled->value != current.value) {
                error = "nativeModifierTieOrderRequired:" + current.source;
                return false;
            }
            previousEnabled = &current;
        }
        terms = std::move(resolved);
        error.clear();
        return true;
    }

    bool EvaluateCanonicalSoulScalar(const SoulSnapshot &snapshot, const SoulClock &clock, const KCDC::Shared::Game::SoulScalarQuery &query, KCDC::Shared::Game::SoulScalarCalculation &result, std::string &error, SoulEvaluationScope scope) {
        std::vector<KCDC::Shared::Game::SoulScalarTerm> terms;
        if (!ResolveCanonicalSoulTerms(snapshot, clock, query.domain, query.selector, terms, error, scope)) {
            return false;
        }
        const auto calculated = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
        if (!calculated) {
            return false;
        }
        result = *calculated;
        return true;
    }
} // namespace KCDC::Features::Player
