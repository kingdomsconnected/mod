#include "c_ui_apse.h"

#include "../c_wh_global_environment.h"
#include "c_gui_module.h"

namespace SDK::wh::guimodule {
    C_UIApse *C_UIApse::GetInstance() {
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        C_GUIModule *guiModule                    = environment != nullptr ? environment->GetGUIModule() : nullptr;
        if (guiModule == nullptr) {
            return nullptr;
        }

        return guiModule->GetApse();
    }
} // namespace SDK::wh::guimodule
