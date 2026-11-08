#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace KCDC::Shared::Game {
    /** Native selectors and units used by the soul scalar API. */
    struct SoulNative final {
        static constexpr std::uint32_t kExperienceMultiplierStat = 47;
        static constexpr std::uint32_t kPotionUpdateAdjustment   = 99;
        // Positive XP conversion 0x180487920; fixed multiply 0x1823D466C.
        static constexpr std::uint32_t kExperienceUnitsPerPoint = 128;
        // S_Constants constructor 0x1810BC57D writes StatCap at +0x138.
        static constexpr float kStatCap = 30.0F;
    };

    enum class SoulScalarDomain : std::uint8_t {
        Stat,
        Skill,
        Adjustment,
        DerivedStat
    };
    // Mod-owned names for the native parser's operation values.
    enum class SoulModifierOperation : std::uint8_t {
        Add,
        Subtract,
        ScaleOriginalBase,
        ScaleCurrent,
        UpperBound,
        LowerBound,
        ReflectCurrent,
        Assign
    };
    enum class SoulQueryReason : std::uint32_t {
        Default       = 0,
        BaseValue     = 1,
        LongTermValue = 2
    };
    struct SoulScalarQuery {
        SoulScalarDomain domain;
        std::uint32_t selector;
        float resolvedBase;
        // Null context differs from a nonnull Default context natively.
        std::optional<SoulQueryReason> context;
    };
    struct SoulScalarTerm {
        SoulScalarDomain domain;
        std::uint32_t selector;
        std::optional<SoulModifierOperation> operation;
        float value;
        bool enabled    = true;
        bool shortTerm  = false;
        bool contextual = false;
        // Only an actual owner-buff context evaluator can provide this.
        std::optional<float> contextWeight;
        std::string source;
    };
    struct SoulScalarCalculation {
        float base;
        float effective;
        std::optional<float> relative;
    };

    /**
     * Arithmetic for an already resolved native chain, in its actual order.
     * This does not collect effects or resolve base-stat overrides. Missing
     * contributing arithmetic or context fails with the source ID.
     */
    std::optional<SoulScalarCalculation> EvaluateResolvedSoulScalar(const SoulScalarQuery &query, std::span<const SoulScalarTerm> orderedTerms, std::string &error);

} // namespace KCDC::Shared::Game
