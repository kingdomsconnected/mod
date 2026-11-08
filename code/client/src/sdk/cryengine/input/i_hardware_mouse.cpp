#include "i_hardware_mouse.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

namespace SDK {
    IHardwareMouse *IHardwareMouse::GetInstance() {
        return SSystemGlobalEnvironment::Get().pHardwareMouse;
    }
} // namespace SDK
