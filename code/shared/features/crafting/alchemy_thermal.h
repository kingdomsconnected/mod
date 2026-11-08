#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace KCDC::Shared::Crafting {
    /** Values passed to the native pot exposure callback at 0x18073920C. */
    enum class AlchemyBoilingState : std::uint8_t {
        None   = 0,
        Weak   = 1,
        Strong = 2
    };

    struct AlchemyHeatStep {
        float heat;
        float weakSeconds;
        float strongSeconds;
        KCDC::Shared::Crafting::AlchemyBoilingState boiling;
    };

    /**
     * One native thermal step, shared by authority and presentation. The
     * caller supplies elapsed time and the membership valid for that entire
     * interval. Authored transfers must split an interval before this call.
     * No network client is allowed to supply these inputs to the authority.
     */
    class AlchemyThermal final {
      public:
        // Authority sampling policy shared by prediction. This is not the
        // game's variable frame time; authored events split these steps.
        static constexpr std::uint64_t kAuthorityStepMilliseconds = 50;
        // C_PotEffectAction constructor 0x1818C29B8, fields +0xB8/+0xBC.
        static constexpr float kWeakHeat    = 1.0F;
        static constexpr float kMaximumHeat = 2.0F;
        // Update 0x180738CBC clamps cooling/heating to this positive floor.
        static constexpr float kMinimumHeat = 0.01F;

        static std::optional<KCDC::Shared::Crafting::AlchemyHeatStep> Step(float heat, float deltaSeconds, bool onFire, bool hasPotResources, float weakBoilTimeout, float strongBoilTimeout) {
            if (!std::isfinite(heat) || heat < kMinimumHeat || heat > kMaximumHeat || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || !std::isfinite(weakBoilTimeout) || weakBoilTimeout <= 0.0F || !std::isfinite(strongBoilTimeout) || strongBoilTimeout <= 0.0F) {
                return std::nullopt;
            }

            // Preserve the operand order at 0x180738CCC..0x180738D38.
            // Native timeouts scale a percentage over minutes, not seconds.
            constexpr float kPercentageScale  = 100.0F;
            constexpr float kSecondsPerMinute = 60.0F;
            const float numerator             = kWeakHeat * (deltaSeconds * kPercentageScale);
            const float weakDenominator       = weakBoilTimeout * kSecondsPerMinute;
            const float strongDenominator     = strongBoilTimeout * kSecondsPerMinute;
            if (!std::isfinite(numerator) || !std::isfinite(weakDenominator) || !std::isfinite(strongDenominator) || weakDenominator <= 0.0F || strongDenominator <= 0.0F) {
                return std::nullopt;
            }
            const float weakDelta   = numerator / weakDenominator;
            const float strongDelta = numerator / strongDenominator;
            if (!std::isfinite(weakDelta) || !std::isfinite(strongDelta)) {
                return std::nullopt;
            }

            KCDC::Shared::Crafting::AlchemyHeatStep result {heat, 0.0F, 0.0F, KCDC::Shared::Crafting::AlchemyBoilingState::None};
            if (!onFire || !hasPotResources) {
                result.heat = std::clamp(heat - weakDelta, kMinimumHeat, kMaximumHeat);
            }
            else if (heat <= kWeakHeat) {
                const float next = heat + weakDelta;
                result.heat      = std::clamp(next, kMinimumHeat, kWeakHeat);
                if (next >= kWeakHeat) {
                    result.boiling = KCDC::Shared::Crafting::AlchemyBoilingState::Weak;
                }
            }
            else {
                const float next = heat - strongDelta;
                result.heat      = std::clamp(next, kWeakHeat, kMaximumHeat);
                result.boiling   = next >= kWeakHeat ? KCDC::Shared::Crafting::AlchemyBoilingState::Strong : KCDC::Shared::Crafting::AlchemyBoilingState::Weak;
            }

            // The native callback credits the selected state for this whole
            // frame, including a threshold-crossing frame. Do not divide it
            // analytically into weak/strong fractions: that changes recipes.
            if (result.boiling == KCDC::Shared::Crafting::AlchemyBoilingState::Weak) {
                result.weakSeconds = deltaSeconds;
            }
            else if (result.boiling == KCDC::Shared::Crafting::AlchemyBoilingState::Strong) {
                result.strongSeconds = deltaSeconds;
            }
            return result;
        }
    };
} // namespace KCDC::Shared::Crafting
