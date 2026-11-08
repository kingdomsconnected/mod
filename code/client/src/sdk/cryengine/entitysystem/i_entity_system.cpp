#include "i_entity_system.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

#include <cstdint>

namespace SDK {
    IEntitySystem *IEntitySystem::GetInstance() {
        return SSystemGlobalEnvironment::Get().pEntitySystem;
    }
} // namespace SDK
