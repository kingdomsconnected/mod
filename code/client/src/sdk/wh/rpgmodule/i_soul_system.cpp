#include "i_soul_system.h"

#include "../c_wh_global_environment.h"
#include "c_soul.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    I_SoulSystem *I_SoulSystem::GetInstance() {
        auto *environment = wh::C_GlobalEnvironment::GetInstance();
        if (environment == nullptr) {
            return nullptr;
        }
        return environment->GetSoulSystem();
    }

    I_Soul *GetOrCreateSoul(I_SoulSystem *soulSystem, IEntity *entity) {
        return soulSystem->GetOrCreateSoul(entity);
    }

    void EnsureSoulInventory(I_Soul *soul) {
        hook::this_call<void>(gPatterns.C_Soul__EnsureInventoryRuntime, soul);
    }
} // namespace SDK::wh::rpgmodule
