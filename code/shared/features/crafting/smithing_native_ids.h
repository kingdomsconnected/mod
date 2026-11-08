#pragma once

#include <cstdint>

namespace KCDC::Shared::Crafting {
    /** Selectors and constants observed in the game's smithing outcome. */
    struct SmithingNative final {
        // C_BlacksmithRecipe::MeetsSkillRequirement 0x18119FD10 queries skill 8.
        static constexpr std::uint32_t kSkill = 8;
        // Derived-stat table 0x1840ACA90[189], token "lfb" at 0x1801C4F53;
        // 0x180648E54 falls through to zero for it.
        static constexpr std::uint32_t kMaximumLockpicksStat = 189;
        static constexpr float kMaximumLockpicksBase         = 0.0F;
        // RPG slot 75 0x182CF8BB4 caps the product tier at 3 without it.
        static constexpr std::uint32_t kSuperiorQualityAbility    = 65;
        static constexpr std::uint32_t kHighestTierWithoutAbility = 3;
        // RPG slot 74 0x182CF3F24: with it, a weapon (class 27) gets the
        // Hardened Steel on-equip buff.
        static constexpr std::uint32_t kHardenedSteelAbility = 87;
        // RPG slot 76 0x182CF8AB4 keeps full condition for this weapon class.
        static constexpr std::int32_t kConditionExemptWeaponClass = 60;
        // wh_pl_BlacksmithReturnIngredientCoef, registered 0.5 at 0x1815A0532.
        // ConsumeRecipeMaterials 0x1812C1950 removes, not returns, this share.
        static constexpr float kFailureMaterialLoss = 0.5F;
        // RPG slot 73 0x180B68718 grants 1..derived-189 of this class.
        static constexpr const char *kLockpick = "8d76f58e-a521-4205-a7e8-9ac077eee5f0";
    };
} // namespace KCDC::Shared::Crafting
