#pragma once

#include "features/prop/prop_editor_figure.h"
#include "features/prop/prop_editor_keymap.h"
#include "world_builder_keymap_ut.h"

#include <cmath>

MODULE(world_builder_figure, {
    IT("faces the scale figure the way the editor camera's yaw reads: counter-clockwise from +Y", {
        const auto facing = [](float yawDegrees) {
            // The body's forward is its own +Y, the second column of its basis.
            const SDK::Matrix34 basis = SDK::Matrix34FromTransform(SDK::Vec3 {}, KCDC::Features::Prop::FigureRotation(yawDegrees), SDK::Vec3 {1.0F, 1.0F, 1.0F});
            return SDK::Vec3 {basis.m[0][1], basis.m[1][1], basis.m[2][1]};
        };
        const auto close = [](const SDK::Vec3 &value, float x, float y, float z) {
            return std::fabs(value.x - x) < 1e-4F && std::fabs(value.y - y) < 1e-4F && std::fabs(value.z - z) < 1e-4F;
        };
        EQUALS(close(facing(0.0F), 0.0F, 1.0F, 0.0F), true);
        EQUALS(close(facing(90.0F), -1.0F, 0.0F, 0.0F), true);
        EQUALS(close(facing(180.0F), 0.0F, -1.0F, 0.0F), true);
        EQUALS(close(facing(-90.0F), 1.0F, 0.0F, 0.0F), true);
        EQUALS(close(facing(450.0F), -1.0F, 0.0F, 0.0F), true);
    });

    IT("looks from the figure's eye height, a step in front of its face", {
        const SDK::Vec3 feet {100.0F, 200.0F, 30.0F};
        const SDK::Vec3 north = KCDC::Features::Prop::FigureEyePoint(feet, 0.0F);
        EQUALS((std::fabs(north.x - 100.0F) < 1e-4F && std::fabs(north.y - (200.0F + KCDC::Features::Prop::kFigureEyeLead)) < 1e-4F && std::fabs(north.z - (30.0F + KCDC::Features::Prop::kFigureEyeHeight)) < 1e-4F), true);
        const SDK::Vec3 west = KCDC::Features::Prop::FigureEyePoint(feet, 90.0F);
        EQUALS((std::fabs(west.x - (100.0F - KCDC::Features::Prop::kFigureEyeLead)) < 1e-4F && std::fabs(west.y - 200.0F) < 1e-4F), true);
    });

    IT("binds standing the figure under the cursor to H in both presets, rebindable by name", {
        WorldBuilderKeymapTests::Context context;
        KCDC::Features::Prop::EditorKeymap keymap;
        EQUALS(keymap.Label(KCDC::Features::Prop::EditorAction::FigureAtCursor) == "H", true);
        EQUALS(keymap.Conflicts().empty(), true);
        EQUALS(keymap.ApplyPreset("wer"), true);
        EQUALS((keymap.Label(KCDC::Features::Prop::EditorAction::FigureAtCursor) == "H" && keymap.Conflicts().empty()), true);
        std::string error;
        EQUALS(keymap.UpdateFromJson(nlohmann::json {{"figureAtCursor", "Ctrl+H"}}, error), true);
        EQUALS(keymap.Label(KCDC::Features::Prop::EditorAction::FigureAtCursor) == "Ctrl+H", true);
    });
});
