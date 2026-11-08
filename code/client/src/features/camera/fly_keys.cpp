#include "fly_keys.h"

#include <utils/safe_win32.h>

#include <input/physical_keys.h>

#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_action.h"
#include "sdk/cryengine/action/i_action_map_action_iterator.h"
#include "sdk/cryengine/action/i_action_map_manager.h"

#include <logging/logger.h>

#include <cstring>
#include <string_view>

namespace KCDC::Features::Camera {
    namespace {
        constexpr const char *kLogger = "Camera";

        constexpr const char *kPhotoModeMap = "photomode_camera";

        /** Actions in `photomode_camera`, in `FlyKey` order. */
        constexpr const char *kActions[] {"photomode_moveforward", "photomode_moveback", "photomode_moveleft", "photomode_moveright", "photomode_moveup", "photomode_movedown", "fast_movement"};
        static_assert(std::size(kActions) == static_cast<std::size_t>(FlyKey::Count), "one action per fly key");

        /** What `keybindSuperactions.xml` ships for each, in the same order. */
        constexpr const char *kDefaults[] {"w", "s", "a", "d", "space", "c", "lshift"};
        static_assert(std::size(kDefaults) == static_cast<std::size_t>(FlyKey::Count), "one default per fly key");

        constexpr std::chrono::seconds kRefreshInterval {1};

        constexpr int kMaxInputs = 8;

        /** CryEngine binding names mapped directly to framework input codes. */
        struct KeyName {
            std::string_view name;
            int code;
        };

        constexpr KeyName kKeyNames[] {
            {"escape", FW_KEY_ESCAPE},
            {"1", '1'},
            {"2", '2'},
            {"3", '3'},
            {"4", '4'},
            {"5", '5'},
            {"6", '6'},
            {"7", '7'},
            {"8", '8'},
            {"9", '9'},
            {"0", '0'},
            {"minus", FW_KEY_OEM_MINUS},
            {"equals", FW_KEY_OEM_PLUS},
            {"backspace", FW_KEY_BACK},
            {"tab", FW_KEY_TAB},
            {"q", 'Q'},
            {"w", 'W'},
            {"e", 'E'},
            {"r", 'R'},
            {"t", 'T'},
            {"y", 'Y'},
            {"u", 'U'},
            {"i", 'I'},
            {"o", 'O'},
            {"p", 'P'},
            {"lbracket", FW_KEY_OEM_4},
            {"rbracket", FW_KEY_OEM_6},
            {"enter", FW_KEY_RETURN},
            {"lctrl", FW_KEY_LCONTROL},
            {"a", 'A'},
            {"s", 'S'},
            {"d", 'D'},
            {"f", 'F'},
            {"g", 'G'},
            {"h", 'H'},
            {"j", 'J'},
            {"k", 'K'},
            {"l", 'L'},
            {"semicolon", FW_KEY_OEM_1},
            {"apostrophe", FW_KEY_OEM_7},
            {"tilde", FW_KEY_OEM_3},
            {"lshift", FW_KEY_LSHIFT},
            {"backslash", FW_KEY_OEM_5},
            {"z", 'Z'},
            {"x", 'X'},
            {"c", 'C'},
            {"v", 'V'},
            {"b", 'B'},
            {"n", 'N'},
            {"m", 'M'},
            {"comma", FW_KEY_OEM_COMMA},
            {"period", FW_KEY_OEM_PERIOD},
            {"slash", FW_KEY_OEM_2},
            {"rshift", FW_KEY_RSHIFT},
            {"np_multiply", FW_KEY_MULTIPLY},
            {"lalt", FW_KEY_LMENU},
            {"space", FW_KEY_SPACE},
            {"capslock", FW_KEY_CAPITAL},
            {"f1", FW_KEY_F1},
            {"f2", FW_KEY_F2},
            {"f3", FW_KEY_F3},
            {"f4", FW_KEY_F4},
            {"f5", FW_KEY_F5},
            {"f6", FW_KEY_F6},
            {"f7", FW_KEY_F7},
            {"f8", FW_KEY_F8},
            {"f9", FW_KEY_F9},
            {"f10", FW_KEY_F10},
            {"numlock", FW_KEY_NUMLOCK},
            {"scrolllock", FW_KEY_SCROLL},
            {"np_7", FW_KEY_NUMPAD7},
            {"np_8", FW_KEY_NUMPAD8},
            {"np_9", FW_KEY_NUMPAD9},
            {"np_subtract", FW_KEY_SUBTRACT},
            {"np_4", FW_KEY_NUMPAD4},
            {"np_5", FW_KEY_NUMPAD5},
            {"np_6", FW_KEY_NUMPAD6},
            {"np_add", FW_KEY_ADD},
            {"np_1", FW_KEY_NUMPAD1},
            {"np_2", FW_KEY_NUMPAD2},
            {"np_3", FW_KEY_NUMPAD3},
            {"np_0", FW_KEY_NUMPAD0},
            {"np_period", FW_KEY_DECIMAL},
            {"oem_102", FW_KEY_OEM_102},
            {"f11", FW_KEY_F11},
            {"f12", FW_KEY_F12},
            {"f13", FW_KEY_F13},
            {"f14", FW_KEY_F14},
            {"f15", FW_KEY_F15},
            {"np_enter", FW_KEY_RETURN},
            {"rctrl", FW_KEY_RCONTROL},
            {"np_divide", FW_KEY_DIVIDE},
            {"print", FW_KEY_SNAPSHOT},
            {"ralt", FW_KEY_RMENU},
            {"pause", FW_KEY_PAUSE},
            {"home", FW_KEY_HOME},
            {"up", FW_KEY_UP},
            {"pgup", FW_KEY_PRIOR},
            {"left", FW_KEY_LEFT},
            {"right", FW_KEY_RIGHT},
            {"end", FW_KEY_END},
            {"down", FW_KEY_DOWN},
            {"pgdn", FW_KEY_NEXT},
            {"insert", FW_KEY_INSERT},
            {"delete", FW_KEY_DELETE},
            {"lwin", FW_KEY_LWIN},
            {"rwin", FW_KEY_RWIN},
            {"apps", FW_KEY_APPS},
        };

        /** False for pad inputs and unknown names. */
        bool Resolve(std::string_view name, bool &mouse, int &code) {
            if (name.size() == 6 && name.substr(0, 5) == "mouse" && name[5] >= '1' && name[5] <= '5') {
                mouse = true;
                code  = name[5] - '1';
                return true;
            }

            for (const KeyName &entry : kKeyNames) {
                if (entry.name != name) {
                    continue;
                }
                mouse = false;
                code  = entry.code;
                return true;
            }
            return false;
        }
    } // namespace

    FlyKeyBindings &FlyKeyBindings::Get() {
        static FlyKeyBindings bindings;
        return bindings;
    }

    void FlyKeyBindings::UseDefaults() {
        for (std::size_t index = 0; index < _keys.size(); ++index) {
            _keys[index].clear();
            Key key;
            if (Resolve(kDefaults[index], key.mouse, key.code)) {
                _keys[index].push_back(key);
            }
        }
    }

    bool FlyKeyBindings::ReadFromGame() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IActionMap *map            = manager != nullptr ? manager->GetActionMap(kPhotoModeMap) : nullptr;
        if (map == nullptr) {
            return false;
        }

        SDK::IActionMapActionIterator *iterator = nullptr;
        map->CreateActionIterator(&iterator);
        if (iterator == nullptr) {
            return false;
        }

        std::array<std::vector<Key>, static_cast<std::size_t>(FlyKey::Count)> keys;
        while (const SDK::IActionMapAction *action = iterator->Next()) {
            const char *name = action->GetActionId().c_str();
            for (std::size_t slot = 0; slot < std::size(kActions); ++slot) {
                if (std::strcmp(name, kActions[slot]) != 0) {
                    continue;
                }

                for (int index = 0; index < kMaxInputs; ++index) {
                    const SDK::SActionInput *input = action->GetActionInput(index);
                    if (input == nullptr) {
                        break;
                    }
                    Key key;
                    if (Resolve(input->GetInput(), key.mouse, key.code)) {
                        keys[slot].push_back(key);
                    }
                }
            }
        }

        // The iterator arrives holding one reference.
        iterator->Release();

        // Still `_keybinds_ref_` placeholders before the controls menu applies.
        for (std::size_t slot = 0; slot < keys.size(); ++slot) {
            if (keys[slot].empty()) {
                Key key;
                if (Resolve(kDefaults[slot], key.mouse, key.code)) {
                    keys[slot].push_back(key);
                }
            }
        }

        _keys = std::move(keys);
        return true;
    }

    void FlyKeyBindings::RefreshIfDue() {
        const auto now = std::chrono::steady_clock::now();
        if (now < _nextRefresh) {
            return;
        }
        _nextRefresh = now + kRefreshInterval;

        const bool fromGame = ReadFromGame();
        if (!fromGame) {
            UseDefaults();
        }
        if (fromGame != _fromGame) {
            _fromGame = fromGame;
            Framework::Logging::GetLogger(kLogger)->debug(fromGame ? "Fly keys follow the game's photo-mode bindings" : "Fly keys fall back to the shipped defaults until the game's action maps are up");
        }
    }

    bool FlyKeyBindings::IsHeld(const Framework::Input::IInput &input, FlyKey key) const {
        for (const Key &bound : _keys[static_cast<std::size_t>(key)]) {
            if (bound.mouse ? input.IsMouseButtonDown(bound.code) : input.IsKeyDown(bound.code)) {
                return true;
            }
        }
        return false;
    }

    void FlyKeyBindings::Read(const Framework::Input::IInput &input, FlyInput &fly) {
        RefreshIfDue();

        const auto axis = [this, &input](FlyKey positive, FlyKey negative) {
            return (IsHeld(input, positive) ? 1.0F : 0.0F) - (IsHeld(input, negative) ? 1.0F : 0.0F);
        };

        fly.forward = axis(FlyKey::Forward, FlyKey::Back);
        fly.strafe  = axis(FlyKey::Right, FlyKey::Left);
        fly.lift    = axis(FlyKey::Up, FlyKey::Down);
        fly.boost   = IsHeld(input, FlyKey::Boost);
        fly.crawl   = input.IsKeyDown(FW_KEY_MENU);
    }

    std::string FlyKeyBindings::Describe(FlyKey key) const {
        const std::vector<Key> &bound = _keys[static_cast<std::size_t>(key)];
        if (bound.empty()) {
            return "?";
        }
        const Key &first = bound.front();
        if (first.mouse) {
            return "Mouse " + std::to_string(first.code + 1);
        }

        const UINT scancode = Framework::Input::PhysicalKeys::ToScanCode(static_cast<UINT>(first.code));
        LONG lParam         = static_cast<LONG>((scancode & 0xFFU) << 16);
        if ((scancode & 0xFF00U) != 0) {
            lParam |= 1L << 24;
        }
        wchar_t text[32] {};
        const int length = GetKeyNameTextW(lParam, text, static_cast<int>(std::size(text)));
        if (length <= 0) {
            return "?";
        }
        char narrow[64] {};
        WideCharToMultiByte(CP_UTF8, 0, text, length, narrow, static_cast<int>(sizeof(narrow)) - 1, nullptr, nullptr);
        return narrow;
    }
} // namespace KCDC::Features::Camera
