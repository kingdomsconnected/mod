#pragma once

#include "sdk/cryengine/input/i_hardware_mouse.h"
#include "sdk/cryengine/system/xml_node_ref.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace KCDC::Core::Boot {
    /** Compiled only with KCDC_AGENT_TESTING. Overrides are never saved. */
    class AgentTesting final {
      public:
        static AgentTesting &Get();
        static void Install();
        void ApplyProfilePreset(const char *file, SDK::XmlNodeRef &document) const;
        std::wstring GetWindowTitle() const;
        void SetNickname(std::string_view nickname);
        /** Main-menu-only, once per process; absent or invalid requests return nullopt. */
        std::optional<std::string> TakeWorldBuilderLevel();

      private:
        AgentTesting();
        mutable std::mutex _titleMutex;
        std::wstring _testIntent;
        std::wstring _nickname = L"not connected";
        static bool __fastcall SaveToFile(SDK::IXmlNode *node, const char *file, unsigned int flags);
        static void __fastcall ConfineCursor(SDK::IHardwareMouse *mouse, bool confine);
        decltype(&SaveToFile) _saveToFileOriginal       = nullptr;
        decltype(&ConfineCursor) _confineCursorOriginal = nullptr;
        int _width                                = 1600;
        int _height                               = 900;
        bool _worldBuilderRequestConsumed         = false;
    };
} // namespace KCDC::Core::Boot
