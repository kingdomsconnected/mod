#include "c_ui_action_hint_manager.h"

#include "../c_wh_global_environment.h"
#include "c_gui_module.h"

namespace SDK::wh::guimodule {
    C_UIActionHintManager *C_UIActionHintManager::GetInstance() {
        C_GlobalEnvironment *environment = C_GlobalEnvironment::GetInstance();
        C_GUIModule *module              = environment != nullptr ? environment->GetGUIModule() : nullptr;
        return module != nullptr ? module->GetActionHintManager() : nullptr;
    }
} // namespace SDK::wh::guimodule
