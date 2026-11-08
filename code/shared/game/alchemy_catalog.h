#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace KCDC::Shared::Game {
    enum class AlchemyPredicateKind : std::uint8_t {
        PotionBase,
        CountEqual,
        CountAtLeast,
        Milled,
        BaseDistilled,
        ResultMilled,
        WeakBoiling,
        StrongBoiling
    };

    struct AlchemyPredicate {
        AlchemyPredicateKind kind;
        std::string_view item;
        float value;
    };

    struct AlchemyStep {
        std::uint32_t id;
        std::uint32_t rewardXP;
        std::uint32_t feedbackPriority;
        std::string_view text;
        // All shipped expressions are conjunctions of these terms. The
        // generator rejects any syntax it cannot compile without ambiguity.
        std::span<const AlchemyPredicate> predicates;
    };

    struct AlchemyProgram {
        std::string_view recipe;
        std::uint32_t baseXP;
        std::span<const AlchemyStep> steps;
    };

    struct AlchemyBase {
        std::string_view item;
        std::string_view kind;
        float quality;
    };

    struct AlchemyHerbVariant {
        std::string_view fresh;
        std::string_view dried;
    };

    extern const std::span<const AlchemyProgram> g_AlchemyPrograms;
    extern const std::span<const AlchemyBase> g_AlchemyBases;
    extern const std::span<const AlchemyHerbVariant> g_AlchemyHerbVariants;
    extern const std::string_view g_AlchemyRulesHash;
} // namespace KCDC::Shared::Game
