#pragma once

#include "features/prop/prop_editor_keymap.h"

#include <imgui.h>

#include <string>

namespace WorldBuilderKeymapTests {
    /** Key names come from ImGui, which wants a context for anything it is asked. */
    struct Context {
        Context() {
            ImGui::CreateContext();
        }
        ~Context() {
            ImGui::DestroyContext();
        }
    };
} // namespace WorldBuilderKeymapTests

MODULE(world_builder_keymap, {
    IT("names and parses chords, modifiers in any case, and refuses unknown keys", {
        WorldBuilderKeymapTests::Context context;
        const KCDC::Features::Prop::KeyChord saveAs {ImGuiKey_S, true, true};
        EQUALS(KCDC::Features::Prop::KeyChordName(saveAs) == "Ctrl+Shift+S", true);
        for (const KCDC::Features::Prop::KeyChord &chord : {saveAs, KCDC::Features::Prop::KeyChord {ImGuiKey_Delete}, KCDC::Features::Prop::KeyChord {ImGuiKey_1}, KCDC::Features::Prop::KeyChord {ImGuiKey_F, false, false, true}}) {
            KCDC::Features::Prop::KeyChord read;
            EQUALS(KCDC::Features::Prop::ParseKeyChord(KCDC::Features::Prop::KeyChordName(chord), read), true);
            EQUALS(read == chord, true);
        }
        KCDC::Features::Prop::KeyChord read;
        EQUALS(KCDC::Features::Prop::ParseKeyChord("ctrl+d", read), true);
        EQUALS((read == KCDC::Features::Prop::KeyChord {ImGuiKey_D, true}), true);
        EQUALS(KCDC::Features::Prop::ParseKeyChord("", read), true);
        EQUALS(read.IsSet(), false);
        EQUALS(KCDC::Features::Prop::ParseKeyChord("Ctrl+Nonsense", read), false);
        EQUALS(KCDC::Features::Prop::ParseKeyChord("Hyper+D", read), false);
        EQUALS(KCDC::Features::Prop::ParseKeyChord("LeftCtrl", read), false);
    });

    IT("ships two presets without conflicts and reports shared chords", {
        WorldBuilderKeymapTests::Context context;
        KCDC::Features::Prop::EditorKeymap keymap;
        EQUALS(keymap.Label(KCDC::Features::Prop::EditorAction::Move) == "1", true);
        EQUALS(keymap.Conflicts().empty(), true);
        EQUALS(keymap.ApplyPreset("wer"), true);
        EQUALS((keymap.Label(KCDC::Features::Prop::EditorAction::Move) == "W" && keymap.Label(KCDC::Features::Prop::EditorAction::Scale) == "R" && keymap.Label(KCDC::Features::Prop::EditorAction::TurnArmed) == "T"), true);
        EQUALS(keymap.Conflicts().empty(), true);
        EQUALS(keymap.ApplyPreset("blender"), false);
        keymap.Set(KCDC::Features::Prop::EditorAction::Frame, KCDC::Features::Prop::KeyChord {ImGuiKey_W});
        EQUALS(keymap.Conflicts().size(), static_cast<std::size_t>(1));
        EQUALS(keymap.Label(KCDC::Features::Prop::EditorAction::SelectSimilar) == "unbound", true);
        // Brush and box-or-lasso keep the same keys in both presets, clear of W E R and the fly keys.
        EQUALS((keymap.Label(KCDC::Features::Prop::EditorAction::ToggleBrush) == "B" && keymap.Label(KCDC::Features::Prop::EditorAction::ToggleLasso) == "L"), true);
    });

    IT("updates bindings from JSON all at once or not at all", {
        WorldBuilderKeymapTests::Context context;
        KCDC::Features::Prop::EditorKeymap keymap;
        std::string error;
        EQUALS(keymap.UpdateFromJson(nlohmann::json {{"move", "W"}, {"selectSimilar", "N"}}, error), true);
        EQUALS((keymap.Label(KCDC::Features::Prop::EditorAction::Move) == "W" && keymap.Label(KCDC::Features::Prop::EditorAction::SelectSimilar) == "N"), true);
        EQUALS(keymap.UpdateFromJson(nlohmann::json {{"rotate", "Q"}, {"scale", "Ctrl+Nonsense"}}, error), false);
        EQUALS(keymap.Label(KCDC::Features::Prop::EditorAction::Rotate) == "2", true);
        EQUALS(keymap.UpdateFromJson(nlohmann::json {{"teleport", "T"}}, error), false);
        EQUALS(error.find("teleport") != std::string::npos, true);
        EQUALS(keymap.UpdateFromJson(nlohmann::json {{"toggleBrush", ""}}, error), true);
        EQUALS(keymap.Chord(KCDC::Features::Prop::EditorAction::ToggleBrush).IsSet(), false);
        KCDC::Features::Prop::EditorKeymap copy;
        EQUALS(copy.UpdateFromJson(keymap.ToJson(), error), true);
        EQUALS(copy.ToJson() == keymap.ToJson(), true);
    });

    IT("round-trips user settings and keeps defaults for what it cannot read", {
        WorldBuilderKeymapTests::Context context;
        KCDC::Features::Prop::EditorUserSettings settings;
        settings.keys.ApplyPreset("wer");
        settings.operation   = 2;
        settings.showHelp    = false;
        settings.brushSelect = true;
        settings.dragLasso   = true;
        settings.brushRadius = 7.5F;
        settings.visibleOnly = true;
        KCDC::Features::Prop::EditorUserSettings read;
        std::string warning;
        read.FromJson(settings.ToJson(), warning);
        EQUALS(warning.empty(), true);
        EQUALS(read.ToJson() == settings.ToJson(), true);

        nlohmann::json damaged = settings.ToJson();
        damaged["operation"]   = 9;
        damaged["keys"]        = {{"move", "Ctrl+Nonsense"}};
        damaged["select"]      = {{"brush", "yes"}, {"brushRadius", 500}};
        read.FromJson(damaged, warning);
        EQUALS(warning.empty(), false);
        EQUALS((read.operation == 0 && !read.brushSelect && read.brushRadius == 3.0F && read.keys.Label(KCDC::Features::Prop::EditorAction::Move) == "1"), true);
        read.FromJson(nlohmann::json::array(), warning);
        EQUALS(read.showHelp, true);
    });

    IT("keeps a user's bindings when their settings name actions this version no longer has", {
        WorldBuilderKeymapTests::Context context;
        // What the build with Click, Box, Lasso and Brush tool keys saved.
        const nlohmann::json earlier = {{"version", 1}, {"keys", {{"rotate", "S"}, {"selectClick", ""}, {"selectBox", ""}, {"selectLasso", ""}, {"selectBrush", ""}}}, {"showHelp", false}, {"select", {{"tool", 3}, {"brushRadius", 6.0}}}};
        KCDC::Features::Prop::EditorUserSettings read;
        std::string warning;
        read.FromJson(earlier, warning);
        EQUALS(warning.empty(), true);
        EQUALS((read.keys.Label(KCDC::Features::Prop::EditorAction::Rotate) == "S" && read.keys.Label(KCDC::Features::Prop::EditorAction::ToggleBrush) == "B"), true);
        EQUALS((!read.showHelp && read.brushRadius == 6.0F && !read.brushSelect), true);
        // MCP still refuses an unknown action outright.
        std::string error;
        EQUALS(read.keys.UpdateFromJson(nlohmann::json {{"selectBox", "B"}}, error), false);
    });
});
