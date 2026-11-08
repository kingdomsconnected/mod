#include "shared/features/crafting/alchemy_thermal.h"

#include <limits>
#include <stdexcept>

namespace {
    void RequireHeat(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
} // namespace

void AlchemyThermalTests() {
    // Use exactly representable increments: 100/(100*60) over 60 seconds
    // changes heat by one. These test the native branch boundaries, not a
    // duplicated implementation of the calculation.
    auto step = KCDC::Shared::Crafting::AlchemyThermal::Step(0.5F, 30.0F, true, true, 100.0F, 100.0F);
    RequireHeat(step && step->heat == 1.0F && step->weakSeconds == 30.0F && step->strongSeconds == 0.0F, "heating across weak threshold credits the whole native frame");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(1.5F, 30.0F, true, true, 100.0F, 100.0F);
    RequireHeat(step && step->heat == 1.0F && step->weakSeconds == 0.0F && step->strongSeconds == 30.0F, "cooling exactly onto threshold keeps strong state for that frame");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(1.5F, 60.0F, true, true, 100.0F, 100.0F);
    RequireHeat(step && step->heat == 1.0F && step->weakSeconds == 60.0F && step->strongSeconds == 0.0F, "cooling below threshold credits weak state without splitting the frame");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(1.0F, 0.0F, true, true, 100.0F, 100.0F);
    RequireHeat(step && step->boiling == KCDC::Shared::Crafting::AlchemyBoilingState::Weak && step->weakSeconds == 0.0F, "zero elapsed time refreshes the boiling presentation without exposure");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(2.0F, 30.0F, false, true, 100.0F, 50.0F);
    RequireHeat(step && step->heat == 1.5F && step->boiling == KCDC::Shared::Crafting::AlchemyBoilingState::None, "off-fire cooling uses weak timeout even above the strong threshold");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(2.0F, 30.0F, true, false, 100.0F, 50.0F);
    RequireHeat(step && step->heat == 1.5F && step->weakSeconds == 0.0F && step->strongSeconds == 0.0F, "empty pot cools and receives no ingredient exposure");
    step = KCDC::Shared::Crafting::AlchemyThermal::Step(0.5F, 60.0F, false, true, 100.0F, 100.0F);
    RequireHeat(step && step->heat == KCDC::Shared::Crafting::AlchemyThermal::kMinimumHeat, "off-fire heat cannot fall below the native positive floor");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0F}) {
        RequireHeat(!KCDC::Shared::Crafting::AlchemyThermal::Step(invalid, 0.1F, true, true, 1.0F, 1.0F), "invalid heat is not normalized into authoritative state");
        RequireHeat(!KCDC::Shared::Crafting::AlchemyThermal::Step(1.0F, invalid, true, true, 1.0F, 1.0F), "invalid elapsed time cannot advance thermal state");
        RequireHeat(!KCDC::Shared::Crafting::AlchemyThermal::Step(1.0F, 0.1F, true, true, invalid, 1.0F), "invalid rule scalar cannot change thermal state");
    }
    RequireHeat(!KCDC::Shared::Crafting::AlchemyThermal::Step(1.0F, 0.1F, true, true, 0.0F, 1.0F), "zero timeout is refused before division");
    RequireHeat(!KCDC::Shared::Crafting::AlchemyThermal::Step(1.0F, std::numeric_limits<float>::max(), true, true, 1.0F, 1.0F), "overflowing elapsed arithmetic is refused");
}
