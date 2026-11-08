#include "../c_wh_global_environment.h"
#include "c_environment_module.h"

namespace SDK::wh::environmentmodule {
    C_EnvironmentModule *C_EnvironmentModule::GetInstance() {
        const auto *environment = C_GlobalEnvironment::GetInstance();
        return environment != nullptr ? environment->GetEnvironmentModule() : nullptr;
    }
} // namespace SDK::wh::environmentmodule
