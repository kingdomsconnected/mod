#pragma once

#include "features/player/soul_evaluator.h"
#include "features/player/soul_profile.h"
#include "shared/features/crafting/alchemy_native_ids.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /** The character inputs brewing reads, resolved for native alchemy's null-context queries. */
    struct AlchemyCharacterContext {
        float skill         = 0.0F;
        float relativeSkill = 0.0F;
        // DerivedStat 184 as it stands now. The table keeps the value it
        // captured on entry; see AlchemyModule::EntryReady.
        float sessionQualityBonus      = 0.0F;
        std::uint32_t allowedMistakes  = 0;
        float boilingTolerance         = 0.0F;
        float freshHerbQuality         = 0.0F;
        float driedHerbQuality         = 0.0F;
        float decayedHerbQuality       = 0.0F;
        std::uint32_t failureXPUnits   = 0;
        std::uint32_t bundleExtraYield = 0;
        bool trialAndError             = false;
        bool bundleAlchemist           = false;
        bool secretOfSecrets           = false;
        bool experimentalPowder        = false;
        std::vector<KCDC::Shared::Game::SoulScalarTerm> yieldTerms;
        std::vector<KCDC::Shared::Game::SoulScalarTerm> scoreTerms;
    };

    /**
     * Stateless: turns a soul snapshot the caller assembled into the inputs
     * the brewing arithmetic needs. XP multipliers are not among them -- the
     * owner's engine applies its own when the server orders the XP.
     */
    class AlchemyCharacterEvaluator final {
      public:
        static constexpr std::uint32_t kExperienceUnitsPerPoint = KCDC::Shared::Game::SoulNative::kExperienceUnitsPerPoint;

        static bool Evaluate(const KCDC::Features::Player::SoulSnapshot &soul, AlchemyCharacterContext &context, std::string &error);
        /** Actual base is supplied for each result, never replaced by a cached scalar. */
        static bool ApplyYield(const AlchemyCharacterContext &context, float base, float &result, std::string &error);
        static bool ApplyScore(const AlchemyCharacterContext &context, float base, float &result, std::string &error);
        /** The `character` object of an alchemy response. `knowledge` is the recipe id to known-step mask map. */
        static nlohmann::json EncodeProjection(const AlchemyCharacterContext &context, const nlohmann::json &knowledge);
    };
} // namespace KCDC::Features::Crafting
