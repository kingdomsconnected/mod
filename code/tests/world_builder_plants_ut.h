#pragma once

#include "features/prop/prop_editor_plants.h"

#include <limits>

MODULE(world_builder_plants, {
    IT("sweeps an object's footprint, its margin and the slope under it for plants", {
        const SDK::AABB house {SDK::Vec3 {10.0F, 20.0F, 5.0F}, SDK::Vec3 {16.0F, 28.0F, 9.0F}};
        KCDC::Features::Prop::PlantFlattening options;
        options.margin               = 0.5F;
        const SDK::AABB sweep        = KCDC::Features::Prop::PlantSweepBox(house, options);
        const SDK::AABB grass        = {SDK::Vec3 {-0.3F, -0.3F, 0.0F}, SDK::Vec3 {0.3F, 0.3F, 0.6F}};
        const auto flattened         = [&](const SDK::Vec3 &root) {
            return KCDC::Features::Prop::IsFlattenedPlant(sweep, root, grass, true, options);
        };
        EQUALS(flattened({13.0F, 24.0F, 5.0F}), true);
        // In the margin, and just past it.
        EQUALS(flattened({9.6F, 24.0F, 5.0F}), true);
        EQUALS(flattened({9.4F, 24.0F, 5.0F}), false);
        EQUALS(flattened({13.0F, 28.6F, 5.0F}), false);
        // Rooted lower on the slope the house hangs over, but not in the cellar of the hill.
        EQUALS(flattened({13.0F, 24.0F, 5.0F - KCDC::Features::Prop::kPlantSweepDepth + 0.1F}), true);
        EQUALS(flattened({13.0F, 24.0F, 5.0F - KCDC::Features::Prop::kPlantSweepDepth - 0.1F}), false);
        // On the roof it is not under the house.
        EQUALS(flattened({13.0F, 24.0F, 9.5F}), false);
    });

    IT("leaves trees and plants with no box standing", {
        const SDK::AABB shed {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {4.0F, 4.0F, 3.0F}};
        KCDC::Features::Prop::PlantFlattening options;
        const SDK::AABB sweep = KCDC::Features::Prop::PlantSweepBox(shed, options);
        const SDK::Vec3 root {2.0F, 2.0F, 0.0F};
        const SDK::AABB bush {SDK::Vec3 {1.5F, 1.5F, 0.0F}, SDK::Vec3 {2.5F, 2.5F, 1.4F}};
        const SDK::AABB tree {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {4.0F, 4.0F, 12.0F}};
        EQUALS(options.maxHeight, 3.0F);
        EQUALS(KCDC::Features::Prop::IsFlattenedPlant(sweep, root, bush, true, options), true);
        EQUALS(KCDC::Features::Prop::IsFlattenedPlant(sweep, root, tree, true, options), false);
        EQUALS(KCDC::Features::Prop::IsFlattenedPlant(sweep, root, bush, false, options), false);
        options.maxHeight = 1.0F;
        EQUALS(KCDC::Features::Prop::IsFlattenedPlant(sweep, root, bush, true, options), false);
    });

    IT("accepts only a margin and a height inside their limits", {
        KCDC::Features::Prop::PlantFlattening options;
        EQUALS(options.IsSane(), true);
        options.margin = KCDC::Features::Prop::PlantFlattening::kMaxMargin + 0.1F;
        EQUALS(options.IsSane(), false);
        options.margin = -0.1F;
        EQUALS(options.IsSane(), false);
        options.margin    = 0.0F;
        options.maxHeight = 0.0F;
        EQUALS(options.IsSane(), false);
        options.maxHeight = std::numeric_limits<float>::quiet_NaN();
        EQUALS(options.IsSane(), false);
        options.maxHeight = KCDC::Features::Prop::PlantFlattening::kMaxMaxHeight;
        EQUALS(options.IsSane(), true);
    });
});
