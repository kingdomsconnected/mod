#pragma once

#include <cstdint>

namespace KCDC::Shared::Crafting {
    /** Numeric identities proven by native RTTR at 0x181631088. */
    enum class AlchemyPosition : std::uint32_t {
        Bowl               = 0,
        Sandglass          = 1,
        Mortar             = 2,
        Pestle             = 3,
        Retort             = 4,
        PotionFlask        = 5,
        Pot                = 6,
        Herb1              = 7,
        Herb2              = 8,
        Herb3              = 9,
        SpecialIngredient1 = 10,
        SpecialIngredient2 = 11,
        SpecialIngredient3 = 12,
        Glass1             = 13,
        Glass2             = 14,
        Glass3             = 15,
        Glass4             = 16,
        FakeFlask          = 17,
        Bellows            = 18,
        Book               = 19,
        PotHalter          = 20,
        None               = 21
    };

    /** Selectors observed in the game's brewing and soul query call sites. */
    struct AlchemyNative final {
        static constexpr std::uint32_t kSkill         = 6;
        static constexpr std::uint32_t kYieldModifier = 73;
        static constexpr std::uint32_t kScoreModifier = 74;
        // Derived-stat table 0x1840ACA90[184], token "bap" at 0x1801C4EDA.
        static constexpr std::uint32_t kSessionQualityBonusStat = 184;
        // 0x180648E54 falls through to zero before derived modifiers apply.
        static constexpr float kSessionQualityBonusBase           = 0.0F;
        static constexpr std::uint32_t kTrialAndErrorAbility      = 19;
        static constexpr std::uint32_t kBundleAlchemistAbility    = 30;
        static constexpr std::uint32_t kSecretOfSecretsAbility    = 63;
        static constexpr std::uint32_t kExperimentalPowderAbility = 92;
        // S_Constants constructor 0x1810BC164 +0xB4, no shipped override.
        static constexpr float kSkillCap = 30.0F;
        // Recipe learning state is a bitfield of 29 authored recipe steps.
        static constexpr std::uint32_t kCompleteRecipeKnowledge = 0x1FFFFFFF;
        // Native CVar registration 0x1815952AC; clamp at 0x182E16834.
        static constexpr std::uint32_t kDefaultDistillationPumps = 2;
        static constexpr std::uint32_t kMinimumDistillationPumps = 1;
        static constexpr std::uint32_t kMaximumDistillationPumps = 100;
        // Shipped Scripts/Entities/WH/Minigames/AlchemyTable.lua.
        static constexpr std::uint32_t kMortarUseCount = 1;
    };
} // namespace KCDC::Shared::Crafting
