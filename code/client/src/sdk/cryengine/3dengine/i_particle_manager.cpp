#include "i_particle_manager.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

namespace SDK {
    IParticleManager *IParticleManager::GetInstance() {
        return SSystemGlobalEnvironment::Get().pParticleManager;
    }
} // namespace SDK
