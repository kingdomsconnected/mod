#include "features/ui/ui_hud_elements.h"
#include "sdk/patterns.h"

#include <iostream>
#include <stdexcept>

// No native game is present. Requests and cleanup must work without touching
// any resolved engine address; rendering is deliberately not exercised here.
namespace SDK {
    Patterns gPatterns {};
}

namespace {
    void Check(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
}

int RunHudElementsSuite() {
    try {
        KCDC::Features::UI::HudElements &hud = KCDC::Features::UI::HudElements::Get();
        hud.Reset();
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "No resource initially suppresses Stats");
        Check(hud.SetVisible("first", SDK::wh::framework::E_HudElements::Type::Stats, false), "Hide must be retained before the engine exists");
        Check(!hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Early hide must be observable");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::QAMWeapon), "Stats must not suppress its weapon sibling");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::QAMFood), "Stats must not suppress its food sibling");

        // A same-value hide still transfers ownership. The earlier resource
        // stopping must not undo the second resource's request.
        hud.SetVisible("second", SDK::wh::framework::E_HudElements::Type::Stats, false);
        hud.Release("first");
        Check(!hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Previous owner must not release current suppression");
        hud.Release("second");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Current owner stopping must release suppression without an engine");

        hud.SetVisible("first", SDK::wh::framework::E_HudElements::Type::Stats, false);
        hud.SetVisible("second", SDK::wh::framework::E_HudElements::Type::Compass, false);
        hud.Release("first");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Resource cleanup must release its own element");
        Check(!hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Compass), "Resource cleanup must preserve another resource's element");

        hud.SetVisible("first", SDK::wh::framework::E_HudElements::Type::Stats, false);
        hud.SetVisible("second", SDK::wh::framework::E_HudElements::Type::Stats, true);
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Explicit enable must release the last hide");
        hud.Release("second");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Stopping a resource must not resurrect a superseded hide");

        hud.SetVisible("first", SDK::wh::framework::E_HudElements::Type::Stats, false);
        hud.Reset();
        for (const std::string_view name : SDK::wh::framework::E_HudElements::kNames) {
            SDK::wh::framework::E_HudElements::Type element {};
            Check(SDK::wh::framework::E_HudElements::FromString(name, element), "Every published HUD name must parse");
            Check(hud.IsVisible(element), "Disconnect must release all resources' suppression");
        }
        hud.Reset();
        hud.Release("first");
        Check(hud.IsVisible(SDK::wh::framework::E_HudElements::Type::Stats), "Repeated cleanup must remain released");

        // The voice indicator is owned the same way: the last hide holds it,
        // and only that resource stopping or the session ending shows it.
        Check(hud.IsVoiceIndicatorVisible(), "No resource initially hides the voice indicator");
        hud.SetVoiceIndicatorVisible("first", false);
        hud.SetVoiceIndicatorVisible("second", false);
        hud.Release("first");
        Check(!hud.IsVoiceIndicatorVisible(), "Previous owner must not release the voice indicator");
        hud.Release("second");
        Check(hud.IsVoiceIndicatorVisible(), "Current owner stopping must show the voice indicator");
        hud.SetVoiceIndicatorVisible("first", false);
        hud.Reset();
        Check(hud.IsVoiceIndicatorVisible(), "Disconnect must show the voice indicator");

        // Story cutscenes stay off until a script releases them, and the next
        // session starts suppressed again.
        Check(!hud.IsStoryCutscenesEnabled(), "Story cutscenes are suppressed by default");
        hud.SetStoryCutscenesEnabled(true);
        Check(hud.IsStoryCutscenesEnabled(), "A script must be able to release story cutscenes");
        hud.Reset();
        Check(!hud.IsStoryCutscenesEnabled(), "Disconnect must suppress story cutscenes again");
        std::cout << "HUD ownership and pre-engine lifecycle tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "HUD tests failed: " << error.what() << '\n';
        return 1;
    }
}
