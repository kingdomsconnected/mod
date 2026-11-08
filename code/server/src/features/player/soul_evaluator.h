#pragma once

#include "soul_profile.h"

#include "shared/game/soul_scalar.h"

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Player {
    enum class SoulEvaluationScope : std::uint8_t {
        CompleteProfile,
        // Only verified brewing skill, quality, yield and XP queries.
        // Unrelated native resource work remains outside server authority.
        AlchemyOutcome,
        // Only the craftsmanship skill and the lockpick byproduct limit, both
        // read during blacksmithing. A contribution the server cannot
        // evaluate is left out rather than refused.
        SmithingOutcome
    };
    /**
     * Resolve proven permanent contributions from the profile's perks and
     * effects. An unknown implementation that contributes stays an explicit
     * blocker; in the alchemy scope a source naming none of the brewing
     * selectors is skipped instead of refused. The smithing scope also skips
     * the contributions it cannot evaluate, and counts `Cpp:Blacksmithing`
     * sources, which apply "during blacksmithing", as active.
     */
    bool ResolveCanonicalSoulTerms(const SoulSnapshot &snapshot, const SoulClock &clock, KCDC::Shared::Game::SoulScalarDomain domain, std::uint32_t selector, std::vector<KCDC::Shared::Game::SoulScalarTerm> &terms, std::string &error, SoulEvaluationScope scope = SoulEvaluationScope::CompleteProfile);
    bool EvaluateCanonicalSoulScalar(const SoulSnapshot &snapshot, const SoulClock &clock, const KCDC::Shared::Game::SoulScalarQuery &query, KCDC::Shared::Game::SoulScalarCalculation &result, std::string &error, SoulEvaluationScope scope = SoulEvaluationScope::CompleteProfile);

    /** Exact native float calendar conversion and inclusive/wrapping window. */
    bool EvaluateSoulDailyWindow(std::uint64_t worldMilliseconds, float startHour, float endHour, bool &active, std::string &error);
} // namespace KCDC::Features::Player
