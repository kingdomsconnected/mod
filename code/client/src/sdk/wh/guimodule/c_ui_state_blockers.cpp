#include "c_ui_state_blockers.h"

#include "../c_wh_global_environment.h"

namespace SDK::wh::guimodule {
    C_UIStateBlockers *C_UIStateBlockers::GetInstance() {
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        return environment != nullptr ? environment->GetUIStateBlockers() : nullptr;
    }
} // namespace SDK::wh::guimodule
