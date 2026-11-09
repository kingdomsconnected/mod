#include <utils/safe_win32.h>

#include "agent_testing.h"

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/string_utils.h>

#include "core/application.h"
#include "sdk/patterns.h"
#include "shared/game/levels.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

namespace KCDC::Core::Boot {
    namespace {
        bool StartsWithPath(std::string_view file, std::string_view prefix) {
            if (file.size() < prefix.size()) {
                return false;
            }
            for (std::size_t index = 0; index < prefix.size(); ++index) {
                char value = file[index] == '\\' ? '/' : file[index];
                if (value >= 'A' && value <= 'Z') {
                    value += 'a' - 'A';
                }
                if (value != prefix[index]) {
                    return false;
                }
            }
            return true;
        }

        struct Attribute {
            const char *name;
            const char *value;
        };

        // Native profile names. res_quality=1 selects sys_spec_Full=1;
        // the individual groups also cover the early graphics reader.
        constexpr Attribute kPreset[] = {
            {"res_fs", "0"},
            {"res_quality", "1"},
            {"r_HDROutput", "0"},
            {"sys_spec_ObjectDetail", "1"},
            {"sys_spec_Shading", "1"},
            {"sys_spec_GlobalIllumination", "1"},
            {"sys_spec_VolumetricEffects", "1"},
            {"sys_spec_Shadows", "1"},
            {"sys_spec_Texture", "1"},
            {"sys_spec_PostProcessing", "1"},
            {"sys_spec_Particles", "1"},
            {"sys_spec_Light", "1"},
            {"sys_spec_Vegetation", "1"},
            {"sys_spec_Characters", "1"},
        };
    } // namespace

    AgentTesting &AgentTesting::Get() {
        static AgentTesting testing;
        return testing;
    }

    AgentTesting::AgentTesting() {
        const DWORD size = GetEnvironmentVariableW(L"KCDC_TEST_INTENT", nullptr, 0);
        if (size > 0) {
            _testIntent.resize(size);
            const DWORD length = GetEnvironmentVariableW(L"KCDC_TEST_INTENT", _testIntent.data(), size);
            _testIntent.resize(length < size ? length : 0);
        }
        const auto first = _testIntent.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) {
            _testIntent = L"intent missing";
        }
        else {
            _testIntent.erase(0, first);
            _testIntent.erase(_testIntent.find_last_not_of(L" \t\r\n") + 1);
        }
    }

    std::optional<std::string> AgentTesting::TakeWorldBuilderLevel() {
        if (_worldBuilderRequestConsumed) {
            return std::nullopt;
        }
        _worldBuilderRequestConsumed = true;
        char level[128] {};
        const DWORD length = GetEnvironmentVariableA("KCDC_TEST_WORLD_BUILDER", level, static_cast<DWORD>(sizeof(level)));
        if (length == 0) {
            return std::nullopt;
        }
        if (length >= sizeof(level) || Shared::Game::FindPlayableLevel(level) == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Agent testing: ignored unsupported KCDC_TEST_WORLD_BUILDER level");
            return std::nullopt;
        }
        return std::string(level, length);
    }

    std::optional<std::string> AgentTesting::TakeConnectLink() {
        char path[MAX_PATH] {};
        const DWORD length = GetEnvironmentVariableA("KCDC_TEST_CONNECT_FILE", path, static_cast<DWORD>(sizeof(path)));
        if (length == 0 || length >= sizeof(path)) {
            return std::nullopt;
        }
        std::ifstream file(path);
        std::string link;
        if (!file.is_open() || !std::getline(file, link)) {
            return std::nullopt;
        }
        file.close();
        std::remove(path);
        return link;
    }

    std::optional<std::string> AgentTesting::WorldBuilderProject() const {
        char path[MAX_PATH] {};
        const DWORD length = GetEnvironmentVariableA("KCDC_TEST_WORLD_BUILDER_PROJECT", path, static_cast<DWORD>(sizeof(path)));
        if (length == 0 || length >= sizeof(path)) {
            return std::nullopt;
        }
        return std::string(path, length);
    }

    bool AgentTesting::AutoAcceptScripts() const {
        char value[16] {};
        const DWORD length = GetEnvironmentVariableA("KCDC_TEST_SCRIPT_APPROVAL", value, static_cast<DWORD>(sizeof(value)));
        return length > 0 && length < sizeof(value) && std::string_view(value, length) == "accept";
    }

    std::wstring AgentTesting::GetWindowTitle() const {
        const std::lock_guard lock(_titleMutex);
        return L"Kingdoms Connected [Agent testing] | " + _testIntent + L" | " + _nickname;
    }

    void AgentTesting::SetNickname(std::string_view nickname) {
        {
            const std::lock_guard lock(_titleMutex);
            _nickname = Framework::Utils::StringUtils::Utf8ToWide(nickname);
        }
        // WM_SETTEXT re-enters the title hook, so release the lock first.
        if (gGlobals.window != nullptr) {
            const auto title = GetWindowTitle();
            SetWindowTextW(gGlobals.window, title.c_str());
        }
    }

    void AgentTesting::ApplyProfilePreset(const char *file, SDK::XmlNodeRef &document) const {
        if ((!StartsWithPath(file, "%user%/profiles/") && !StartsWithPath(file, "libs/config/profiles/")) || !document->isTag("Attributes")) {
            return;
        }

        // Packaged defaults can be read-only XML; clone before editing. Both
        // CSystem's startup reader and the later profile manager receive the
        // preset, without changing the file or its schema version.
        auto edited             = document->clone(true);
        const auto setAttribute = [&edited](const Attribute &attribute) {
            bool found = false;
            for (int index = 0; index < edited->getChildCount(); ++index) {
                auto child = edited->getChild(index);
                if (child->isTag("Attr") && std::strcmp(child->getAttr("name"), attribute.name) == 0) {
                    child->setAttr("value", attribute.value);
                    found = true;
                }
            }
            if (!found) {
                auto child = edited->newChild("Attr");
                child->setAttr("name", attribute.name);
                child->setAttr("value", attribute.value);
            }
        };
        for (const auto &attribute : kPreset) {
            setAttribute(attribute);
        }
        const auto width  = std::to_string(_width);
        const auto height = std::to_string(_height);
        setAttribute({"res_x", width.c_str()});
        setAttribute({"res_y", height.c_str()});
        document = std::move(edited);
    }

    bool __fastcall AgentTesting::SaveToFile(SDK::IXmlNode *node, const char *file, unsigned int flags) {
        // Covers attributes.xml, profile.xml and settings.xml. Return success
        // to the native options UI, but leave the user's profile on disk alone.
        if (file != nullptr && StartsWithPath(file, "%user%/profiles/")) {
            return true;
        }
        return Get()._saveToFileOriginal(node, file, flags);
    }

    void __fastcall AgentTesting::ConfineCursor(SDK::IHardwareMouse *mouse, bool confine) {
        // CHardwareMouse::ConfineCursor clips the cursor to the window. The
        // game asks for it while the cursor is hidden -- mouse look, which
        // needs it, or the window would lose the mouse -- and also while
        // input flag 40 is set whether the cursor shows or not
        // (EvaluateCursorConfinement 0x1808EB278, IncrementCounter
        // 0x1808EAF8C). A visible cursor is never clipped here, so whoever
        // drives several test clients can reach the others through a menu.
        Get()._confineCursorOriginal(mouse, confine && !mouse->IsCursorVisible());
    }

    void AgentTesting::Install() {
        // The native renderer starts on the primary monitor. Reserve the
        // taskbar, a normal window frame and a small margin; keep exact 16:9.
        const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        MONITORINFO monitor    = {sizeof(MONITORINFO)};
        RECT frame             = {};
        if (GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) && AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem())) {
            const auto units = std::max(1L, std::min({100L, (monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left) - 24) / 16, (monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top) - 24) / 9}));
            Get()._width     = static_cast<int>(units * 16);
            Get()._height    = static_cast<int>(units * 9);
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Agent testing: could not measure desktop; using 1600x900");
        }
        if (previousDpi != nullptr) {
            SetThreadDpiAwarenessContext(previousDpi);
        }
        const auto result = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CXmlNode__SaveToFile), reinterpret_cast<LPVOID>(&SaveToFile), reinterpret_cast<LPVOID *>(&Get()._saveToFileOriginal));
        if (result != MH_OK) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Agent testing: could not protect profile writes ({})", MH_StatusToString(result));
            ExitProcess(EXIT_FAILURE);
        }
        if (const auto confine = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CHardwareMouse__ConfineCursor), reinterpret_cast<LPVOID>(&ConfineCursor), reinterpret_cast<LPVOID *>(&Get()._confineCursorOriginal)); confine != MH_OK) {
            // Only a convenience: the client still clips a visible cursor when the game asks.
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Agent testing: could not free the cursor ({})", MH_StatusToString(confine));
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing enabled; native profile changes will not be saved, and the cursor is clipped to the window only while it is hidden");
    }
} // namespace KCDC::Core::Boot

static InitFunction init(&KCDC::Core::Boot::AgentTesting::Install, "AgentTesting");
