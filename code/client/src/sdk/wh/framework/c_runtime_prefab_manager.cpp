#include "c_runtime_prefab_manager.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::framework {
    C_RuntimePrefabManager *C_RuntimePrefabManager::GetInstance() {
        auto *game = *reinterpret_cast<std::byte **>(gPatterns.C_Game__Instance);
        if (game == nullptr) {
            return nullptr;
        }

        return *reinterpret_cast<C_RuntimePrefabManager **>(game + 0x150);
    }

    void C_RuntimePrefabManager::InvokeCreatePrefabRaw(
        const CryGUID &prefabTemplateId, EntityId entityId, int maxSpawn,
        bool spawnImmediately) {
        hook::this_call<void>(gPatterns.C_RuntimePrefabManager__CreatePrefabRaw, this, prefabTemplateId, entityId, maxSpawn, spawnImmediately);
    }

    void C_RuntimePrefabManager::InvokeMovePrefabRaw(EntityId entityId) {
        hook::this_call<void>(gPatterns.C_RuntimePrefabManager__MovePrefabRaw, this, entityId);
    }

    void C_RuntimePrefabManager::InvokeDeletePrefabRaw(EntityId entityId) {
        hook::this_call<void>(gPatterns.C_RuntimePrefabManager__DeletePrefabRaw, this, entityId);
    }
} // namespace SDK::wh::framework
