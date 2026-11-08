#include "s_system_global_environment.h"

#include "../../patterns.h"

namespace SDK {
    SSystemGlobalEnvironment &SSystemGlobalEnvironment::Get() {
        return *reinterpret_cast<SSystemGlobalEnvironment *>(gPatterns.SSystemGlobalEnvironment__Base);
    }
} // namespace SDK
