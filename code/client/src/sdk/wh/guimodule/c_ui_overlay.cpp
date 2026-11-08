#include "c_ui_overlay.h"

#include "../../patterns.h"
#include "../c_wh_global_environment.h"
#include "c_gui_module.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    C_UIOverlay *C_UIOverlay::GetInstance() {
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        C_GUIModule *guiModule                    = environment != nullptr ? environment->GetGUIModule() : nullptr;
        if (guiModule == nullptr) {
            return nullptr;
        }

        return guiModule->GetOverlay();
    }

    void C_UIOverlay::ApplyUserColor(bool enabled) {
        hook::this_call<void>(gPatterns.C_UIOverlay__ApplyUserColor, this, enabled);
    }
} // namespace SDK::wh::guimodule
