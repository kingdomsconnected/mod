#include "soul_scalar.h"
#include "shared/features/crafting/alchemy_native_ids.h"
#include "shared/features/crafting/smithing_native_ids.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Shared::Game {
    namespace {
        constexpr double kMaximumNativeLevelExclusive = static_cast<double>(std::numeric_limits<std::uint32_t>::max()) + 1.0;

        template <typename T>
        std::optional<T> Refuse(std::string &error, const char *reason, const std::string &source = {}) {
            error = reason;
            if (!source.empty()) {
                error += ":" + source;
            }
            return std::nullopt;
        }
        bool IsProgression(SoulScalarDomain domain) {
            return domain == SoulScalarDomain::Stat || domain == SoulScalarDomain::Skill;
        }
    } // namespace

    std::optional<SoulScalarCalculation> EvaluateResolvedSoulScalar(const SoulScalarQuery &query, std::span<const SoulScalarTerm> orderedTerms, std::string &error) {
        if (!std::isfinite(query.resolvedBase) || query.domain > SoulScalarDomain::DerivedStat || (query.context && *query.context > SoulQueryReason::LongTermValue)) {
            return Refuse<SoulScalarCalculation>(error, "unsupportedScalarQuery");
        }
        if (IsProgression(query.domain) && (query.resolvedBase < 0.0F || std::floor(query.resolvedBase) != query.resolvedBase || static_cast<double>(query.resolvedBase) >= kMaximumNativeLevelExclusive)) {
            return Refuse<SoulScalarCalculation>(error, "nonIntegralNativeBaseLevel");
        }
        if (query.domain == SoulScalarDomain::Adjustment && query.selector != KCDC::Shared::Crafting::AlchemyNative::kYieldModifier && query.selector != KCDC::Shared::Crafting::AlchemyNative::kScoreModifier && query.selector != SoulNative::kPotionUpdateAdjustment) {
            return Refuse<SoulScalarCalculation>(error, "unsupportedAdjustmentBounds");
        }

        if (query.domain == SoulScalarDomain::DerivedStat && query.selector != SoulNative::kExperienceMultiplierStat && query.selector != KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusStat && query.selector != KCDC::Shared::Crafting::SmithingNative::kMaximumLockpicksStat) {
            return Refuse<SoulScalarCalculation>(error, "unsupportedDerivedStatBounds");
        }

        float current = query.resolvedBase;
        for (const auto &term : orderedTerms) {
            if (!term.enabled || term.domain != query.domain || term.selector != query.selector) {
                continue;
            }
            // 0x180469FF8 suppresses these before calling a buff's weight
            // evaluator; an irrelevant unsupported rule cannot block them.
            if (query.context == SoulQueryReason::BaseValue || (query.context == SoulQueryReason::LongTermValue && term.shortTerm) || (term.contextual && !query.context)) {
                continue;
            }
            const auto weight = term.contextual ? term.contextWeight : std::optional<float>(1.0F);
            if (!weight) {
                return Refuse<SoulScalarCalculation>(error, "unsupportedContextWeight", term.source);
            }
            if (!std::isfinite(*weight)) {
                return Refuse<SoulScalarCalculation>(error, "invalidContextWeight", term.source);
            }
            if (*weight == 0.0F) {
                continue;
            }
            if (!term.operation || *term.operation > SoulModifierOperation::Assign) {
                return Refuse<SoulScalarCalculation>(error, "unsupportedContributingOperator", term.source);
            }
            if (!std::isfinite(term.value)) {
                return Refuse<SoulScalarCalculation>(error, "invalidContributingValue", term.source);
            }
            switch (*term.operation) {
            case SoulModifierOperation::Add: current = current + *weight * term.value; break;
            case SoulModifierOperation::Subtract: current = current - *weight * term.value; break;
            case SoulModifierOperation::ScaleOriginalBase: current = current + ((term.value - 1.0F) * query.resolvedBase) * *weight; break;
            case SoulModifierOperation::ScaleCurrent: current = current + ((term.value - 1.0F) * current) * *weight; break;
            case SoulModifierOperation::UpperBound: current = std::min(current, term.value); break;
            case SoulModifierOperation::LowerBound: current = std::max(current, term.value); break;
            case SoulModifierOperation::ReflectCurrent: current = term.value - current; break;
            case SoulModifierOperation::Assign: current = term.value; break;
            }
            // Fail closed on non-finite arithmetic rather than claim native
            // parity for those inputs.
            if (!std::isfinite(current)) {
                return Refuse<SoulScalarCalculation>(error, "modifierArithmeticOverflow", term.source);
            }
        }
        const float maximum = query.domain == SoulScalarDomain::Skill ? KCDC::Shared::Crafting::AlchemyNative::kSkillCap : query.domain == SoulScalarDomain::Stat ? SoulNative::kStatCap : std::numeric_limits<float>::max();
        SoulScalarCalculation result {query.resolvedBase, std::clamp(current, 0.0F, maximum), std::nullopt};
        if (IsProgression(query.domain)) {
            result.relative = result.effective / maximum;
        }
        error.clear();
        return result;
    }

} // namespace KCDC::Shared::Game
