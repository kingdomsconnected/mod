#include "i_input.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

#include <cstdint>

namespace SDK {
    IInput *IInput::GetInstance() {
        return SSystemGlobalEnvironment::Get().pInput;
    }
} // namespace SDK
