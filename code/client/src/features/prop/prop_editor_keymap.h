#pragma once

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <utils/string_utils.h>

namespace KCDC::Features::Prop {
    /** One key with the modifiers that must be held with it, and no others. */
    struct KeyChord {
        ImGuiKey key = ImGuiKey_None;
        bool ctrl    = false;
        bool shift   = false;
        bool alt     = false;

        bool IsSet() const {
            return key != ImGuiKey_None;
        }

        bool operator==(const KeyChord &other) const = default;
    };

    /** A chord as the editor shows and saves it: `Ctrl+Shift+S`, `Delete`, `1`. Empty when unbound. */
    inline std::string KeyChordName(const KeyChord &chord) {
        if (!chord.IsSet()) {
            return {};
        }
        std::string name;
        if (chord.ctrl) {
            name += "Ctrl+";
        }
        if (chord.shift) {
            name += "Shift+";
        }
        if (chord.alt) {
            name += "Alt+";
        }
        return name + ImGui::GetKeyName(chord.key);
    }

    /** Whether a named key can be a chord's key: not a modifier on its own, and not the mouse. */
    inline bool IsChordKey(ImGuiKey key) {
        return key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_NamedKey_END && !(key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper) && !(key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
               && !(key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY);
    }

    /** Reads what KeyChordName writes; modifier and key names ignore case. An empty string is unbound. False for an unknown key. */
    inline bool ParseKeyChord(std::string_view text, KeyChord &chord) {
        chord = KeyChord {};
        if (text.empty()) {
            return true;
        }
        std::size_t start = 0;
        while (true) {
            const std::size_t plus = text.find('+', start);
            // A trailing "+" is the key itself, as in "Ctrl++".
            if (plus == std::string_view::npos || plus + 1 == text.size()) {
                break;
            }
            const std::string_view modifier = text.substr(start, plus - start);
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(modifier, "Ctrl")) {
                chord.ctrl = true;
            }
            else if (Framework::Utils::StringUtils::EqualsIgnoreCase(modifier, "Shift")) {
                chord.shift = true;
            }
            else if (Framework::Utils::StringUtils::EqualsIgnoreCase(modifier, "Alt")) {
                chord.alt = true;
            }
            else {
                return false;
            }
            start = plus + 1;
        }
        const std::string_view keyName = text.substr(start);
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            if (!IsChordKey(static_cast<ImGuiKey>(key))) {
                continue;
            }
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(ImGui::GetKeyName(static_cast<ImGuiKey>(key)), keyName)) {
                chord.key = static_cast<ImGuiKey>(key);
                return true;
            }
        }
        chord = KeyChord {};
        return false;
    }

    /** What the editor's keys do. The order is the Keys tab's. */
    enum class EditorAction : std::uint8_t {
        Move,
        Rotate,
        Scale,
        ToggleAxes,
        ToggleSnap,
        Frame,
        DropToGround,
        Delete,
        Duplicate,
        Undo,
        Redo,
        Save,
        SaveAs,
        CopyApi,
        AssignLayer,
        TurnArmed,
        ToggleBrush,
        ToggleLasso,
        SelectSimilar,
        FigureAtCursor,
        Count,
    };

    struct EditorActionInfo {
        const char *id;    // what the settings file and MCP call it
        const char *label; // what the Keys tab calls it
        KeyChord standard; // the Default preset
        KeyChord wer;      // the W E R preset
    };

    /** Every action, in EditorAction order. */
    inline const std::array<EditorActionInfo, static_cast<std::size_t>(EditorAction::Count)> &EditorActions() {
        static const std::array<EditorActionInfo, static_cast<std::size_t>(EditorAction::Count)> actions {{
            {"move", "Move tool", {ImGuiKey_1}, {ImGuiKey_W}},
            {"rotate", "Rotate tool", {ImGuiKey_2}, {ImGuiKey_E}},
            {"scale", "Scale tool", {ImGuiKey_3}, {ImGuiKey_R}},
            {"toggleAxes", "Local or world axes", {ImGuiKey_G}, {ImGuiKey_G}},
            {"toggleSnap", "Snapping", {ImGuiKey_X}, {ImGuiKey_X}},
            {"frame", "Frame the selection", {ImGuiKey_F}, {ImGuiKey_F}},
            {"dropToGround", "Drop to the ground", {ImGuiKey_End}, {ImGuiKey_End}},
            {"delete", "Delete, or take the level's own out", {ImGuiKey_Delete}, {ImGuiKey_Delete}},
            {"duplicate", "Duplicate", {ImGuiKey_D, true}, {ImGuiKey_D, true}},
            {"undo", "Undo", {ImGuiKey_Z, true}, {ImGuiKey_Z, true}},
            {"redo", "Redo (Ctrl+Shift+Z also works)", {ImGuiKey_Y, true}, {ImGuiKey_Y, true}},
            {"save", "Save the project", {ImGuiKey_S, true}, {ImGuiKey_S, true}},
            {"saveAs", "Save the project as", {ImGuiKey_S, true, true}, {ImGuiKey_S, true, true}},
            {"copyApi", "Copy the selection as API calls", {ImGuiKey_C, true}, {ImGuiKey_C, true}},
            {"assignLayer", "Put the selection on the active layer", {ImGuiKey_L, true}, {ImGuiKey_L, true}},
            // Shift turns the other way, so Shift is not part of its chord.
            {"turnArmed", "Turn the mesh being placed", {ImGuiKey_R}, {ImGuiKey_T}},
            {"toggleBrush", "Brush selection on or off", {ImGuiKey_B}, {ImGuiKey_B}},
            {"toggleLasso", "Drag selects by box or by lasso", {ImGuiKey_L}, {ImGuiKey_L}},
            {"selectSimilar", "Select similar nearby", {}, {}},
            {"figureAtCursor", "Stand the scale figure under the cursor", {ImGuiKey_H}, {ImGuiKey_H}},
        }};
        return actions;
    }

    /**
     * The editor's key bindings: one chord per action, which may be unbound.
     * Render thread only. Conflicts are reported, not refused, so a binding
     * can be moved through a state where two actions share a key.
     */
    class EditorKeymap final {
      public:
        EditorKeymap() {
            ApplyPreset("default");
        }

        const KeyChord &Chord(EditorAction action) const {
            return _chords[static_cast<std::size_t>(action)];
        }

        void Set(EditorAction action, const KeyChord &chord) {
            _chords[static_cast<std::size_t>(action)] = chord;
        }

        /** `default` (1 2 3) or `wer` (W E R). False for another name. */
        bool ApplyPreset(std::string_view preset) {
            const bool standard = preset == "default";
            if (!standard && preset != "wer") {
                return false;
            }
            for (std::size_t index = 0; index < _chords.size(); ++index) {
                _chords[index] = standard ? EditorActions()[index].standard : EditorActions()[index].wer;
            }
            return true;
        }

        /** The chord's name, or `unbound`. */
        std::string Label(EditorAction action) const {
            const std::string name = KeyChordName(Chord(action));
            return name.empty() ? std::string {"unbound"} : name;
        }

        /** Pairs of actions bound to the same chord. */
        std::vector<std::pair<EditorAction, EditorAction>> Conflicts() const {
            std::vector<std::pair<EditorAction, EditorAction>> conflicts;
            for (std::size_t first = 0; first < _chords.size(); ++first) {
                for (std::size_t second = first + 1; second < _chords.size(); ++second) {
                    if (_chords[first].IsSet() && _chords[first] == _chords[second]) {
                        conflicts.emplace_back(static_cast<EditorAction>(first), static_cast<EditorAction>(second));
                    }
                }
            }
            return conflicts;
        }

        /** Whether this frame pressed the action's chord. `anyShift` lets Shift be held or not, for actions Shift reverses. */
        bool Pressed(EditorAction action, bool repeat = false, bool anyShift = false) const {
            const KeyChord &chord = Chord(action);
            if (!chord.IsSet() || !ImGui::IsKeyPressed(chord.key, repeat)) {
                return false;
            }
            const ImGuiIO &io = ImGui::GetIO();
            return io.KeyCtrl == chord.ctrl && io.KeyAlt == chord.alt && (anyShift || io.KeyShift == chord.shift);
        }

        /** `{ "move": "1", ... }`: every action, unbound ones as empty strings. */
        nlohmann::json ToJson() const {
            nlohmann::json keys = nlohmann::json::object();
            for (std::size_t index = 0; index < _chords.size(); ++index) {
                keys[EditorActions()[index].id] = KeyChordName(_chords[index]);
            }
            return keys;
        }

        /** Applies the bindings `keys` names over the current ones; actions it leaves out keep theirs. Nothing changes when one is invalid. */
        bool UpdateFromJson(const nlohmann::json &keys, std::string &error) {
            if (!keys.is_object()) {
                error = "keys must be an object of action names to chords";
                return false;
            }
            std::array<KeyChord, static_cast<std::size_t>(EditorAction::Count)> next = _chords;
            for (const auto &[id, value] : keys.items()) {
                const std::size_t index = IndexOf(id);
                if (index == _chords.size()) {
                    error = "unknown action '" + id + "'";
                    return false;
                }
                if (!value.is_string() || !ParseKeyChord(value.get<std::string>(), next[index])) {
                    error = "'" + id + "' needs a chord such as Ctrl+D, or an empty string to unbind it";
                    return false;
                }
            }
            _chords = next;
            return true;
        }

        static std::size_t IndexOf(std::string_view id) {
            for (std::size_t index = 0; index < EditorActions().size(); ++index) {
                if (id == EditorActions()[index].id) {
                    return index;
                }
            }
            return EditorActions().size();
        }

      private:
        std::array<KeyChord, static_cast<std::size_t>(EditorAction::Count)> _chords {};
    };

    /**
     * What the editor keeps between sessions for this user rather than for
     * a project: its keys, the tool in hand and the selection tools' sizes.
     * Saved as `editor.settings.json` beside the projects.
     */
    struct EditorUserSettings {
        static constexpr int kVersion = 1;

        EditorKeymap keys;
        int operation       = 0; // 0 move, 1 rotate, 2 scale
        bool uniformScale   = true;
        bool showHelp       = true;
        bool brushSelect    = false; // a drag paints with the brush
        bool dragLasso      = false; // otherwise a drag draws a lasso rather than a box
        float brushRadius   = 3.0F;
        float selectReach   = 150.0F;
        float similarRadius = 50.0F;
        bool visibleOnly    = false;

        nlohmann::json ToJson() const {
            return {{"version", kVersion}, {"keys", keys.ToJson()}, {"operation", operation}, {"uniformScale", uniformScale}, {"showHelp", showHelp},
                {"select", {{"brush", brushSelect}, {"lasso", dragLasso}, {"brushRadius", brushRadius}, {"reach", selectReach}, {"similarRadius", similarRadius}, {"visibleOnly", visibleOnly}}}};
        }

        /** Reads what ToJson writes. A missing or unreadable field keeps its default; `warning` says what was ignored. */
        void FromJson(const nlohmann::json &node, std::string &warning) {
            *this = EditorUserSettings {};
            if (!node.is_object()) {
                warning = "the editor settings are not a JSON object; defaults are used";
                return;
            }
            if (const auto found = node.find("keys"); found != node.end()) {
                // An action an earlier version had is dropped quietly, rather than costing the user every other binding.
                nlohmann::json known = *found;
                if (found->is_object()) {
                    for (const auto &[id, value] : found->items()) {
                        if (EditorKeymap::IndexOf(id) == EditorActions().size()) {
                            known.erase(id);
                        }
                    }
                }
                std::string error;
                if (!keys.UpdateFromJson(known, error)) {
                    warning = "key bindings ignored: " + error;
                }
            }
            const auto integer = [&node](const char *key, int low, int high, int &out) {
                if (const auto found = node.find(key); found != node.end() && found->is_number_integer() && found->get<int>() >= low && found->get<int>() <= high) {
                    out = found->get<int>();
                }
            };
            const auto boolean = [](const nlohmann::json &parent, const char *key, bool &out) {
                if (const auto found = parent.find(key); found != parent.end() && found->is_boolean()) {
                    out = found->get<bool>();
                }
            };
            const auto number = [](const nlohmann::json &parent, const char *key, float low, float high, float &out) {
                if (const auto found = parent.find(key); found != parent.end() && found->is_number() && found->get<float>() >= low && found->get<float>() <= high) {
                    out = found->get<float>();
                }
            };
            integer("operation", 0, 2, operation);
            boolean(node, "uniformScale", uniformScale);
            boolean(node, "showHelp", showHelp);
            if (const auto select = node.find("select"); select != node.end() && select->is_object()) {
                boolean(*select, "brush", brushSelect);
                boolean(*select, "lasso", dragLasso);
                number(*select, "brushRadius", 0.25F, 50.0F, brushRadius);
                number(*select, "reach", 5.0F, 250.0F, selectReach);
                number(*select, "similarRadius", 1.0F, 250.0F, similarRadius);
                boolean(*select, "visibleOnly", visibleOnly);
            }
        }
    };
} // namespace KCDC::Features::Prop
