#include "c_ui_loading_screen.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    void C_UILoadingScreen::StopVideoOrLoadingScreen() {
        hook::this_call<void>(gPatterns.C_UILoadingScreen__StopVideoOrLoadingScreen, this);
    }

    void C_UILoadingScreen::ReturnToMainMenuState() {
        hook::this_call<void>(gPatterns.C_UILoadingScreen__ReturnToMainMenuState, this);
    }
} // namespace SDK::wh::guimodule
