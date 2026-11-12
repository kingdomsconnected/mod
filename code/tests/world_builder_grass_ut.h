#pragma once

#include "features/prop/prop_editor_document.h"
#include "features/prop/prop_editor_grass.h"
#include "shared/features/grass/grass_clearing.h"
#include "shared/features/level_edit/level_edit_definition.h"

#include <cmath>
#include <cstddef>
#include <string>

MODULE(world_builder_grass, {
    IT("clears a house-shaped patch under an upright object and a world box under a tilted one", {
        const SDK::AABB local {SDK::Vec3 {-4.0F, -2.0F, 0.0F}, SDK::Vec3 {4.0F, 2.0F, 3.0F}};
        const SDK::Vec3 position {100.0F, 200.0F, 50.0F};
        const SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        KCDC::Features::Prop::GrassClearingOptions options;
        options.margin = 0.5F;

        // Turned a quarter about the vertical: its world box is 4 by 8, and so is the clearing, but as a turned 8 by 4.
        const float half = std::sqrt(0.5F);
        const SDK::Quat quarter {0.0F, 0.0F, half, half};
        const SDK::AABB world {SDK::Vec3 {98.0F, 196.0F, 50.0F}, SDK::Vec3 {102.0F, 204.0F, 53.0F}};
        KCDC::Shared::Game::GrassClearing box;
        EQUALS(KCDC::Features::Prop::GrassClearingUnder(position, quarter, scale, &local, world, options, box), true);
        EQUALS(std::fabs(box.yawDegrees - 90.0F) < 1.0e-3F, true);
        EQUALS(std::fabs(box.halfSize.x - 4.5F) < 1.0e-4F, true);
        EQUALS(std::fabs(box.halfSize.y - 2.5F) < 1.0e-4F, true);
        EQUALS(box.Contains({100.0F, 204.4F, 50.0F}), true);
        EQUALS(box.Contains({102.4F, 200.0F, 50.0F}), true);
        EQUALS(box.Contains({102.6F, 200.0F, 50.0F}), false);
        // From a little under its foot, for a floor set on a slope, to its top.
        EQUALS(box.Contains({100.0F, 200.0F, 50.0F - KCDC::Features::Prop::kGrassClearingDepth + 0.1F}), true);
        EQUALS(box.Contains({100.0F, 200.0F, 50.0F - KCDC::Features::Prop::kGrassClearingDepth - 0.1F}), false);
        EQUALS(box.Contains({100.0F, 200.0F, 53.1F}), false);

        // An off-centre mesh clears where it stands, not around its pivot.
        const SDK::AABB offCentre {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {8.0F, 4.0F, 3.0F}};
        const SDK::AABB offCentreWorld {SDK::Vec3 {96.0F, 200.0F, 50.0F}, SDK::Vec3 {100.0F, 208.0F, 53.0F}};
        EQUALS(KCDC::Features::Prop::GrassClearingUnder(position, quarter, scale, &offCentre, offCentreWorld, options, box), true);
        EQUALS(std::fabs(box.center.x - 98.0F) < 1.0e-3F, true);
        EQUALS(std::fabs(box.center.y - 204.0F) < 1.0e-3F, true);

        // Tipped over, only the box around it means anything.
        const SDK::Quat tipped {half, 0.0F, 0.0F, half};
        EQUALS(KCDC::Features::Prop::GrassClearingUnder(position, tipped, scale, &local, world, options, box), true);
        EQUALS(box.yawDegrees, 0.0F);
        EQUALS(std::fabs(box.halfSize.x - 2.5F) < 1.0e-4F, true);
        EQUALS(std::fabs(box.halfSize.y - 4.5F) < 1.0e-4F, true);

        // So does an object with no box of its own.
        EQUALS(KCDC::Features::Prop::GrassClearingUnder(position, quarter, scale, nullptr, world, options, box), true);
        EQUALS(box.yawDegrees, 0.0F);

        options.margin = KCDC::Features::Prop::GrassClearingOptions::kMaxMargin + 1.0F;
        EQUALS(KCDC::Features::Prop::GrassClearingUnder(position, quarter, scale, &local, world, options, box), false);
    });

    IT("saves a clearing with the project and reads the same project back", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "trosecko";
        KCDC::Shared::Game::LevelEditDefinition grass;
        grass.kind       = KCDC::Shared::Game::LevelEditKind::Grass;
        grass.at         = {1234.5F, 678.25F, 91.0F};
        grass.halfSize   = {6.1F, 4.3F, 2.5F};
        grass.yawDegrees = 33.3F;
        grass.hidden     = true;
        document.world.push_back(grass);

        std::string error;
        const std::string text = KCDC::Features::Prop::SerializeProject(document);
        KCDC::Features::Prop::MapDocument loaded;
        EQUALS(KCDC::Features::Prop::ParseProject(text, loaded, error), true);
        EQUALS(loaded.world.size(), std::size_t {1});
        EQUALS(loaded.world[0].Clearing() == grass.Clearing(), true);
        // The unsaved check compares the two as text: an unchanged project must not read as dirty.
        EQUALS(KCDC::Features::Prop::SerializeProject(loaded) == text, true);
    });
});
