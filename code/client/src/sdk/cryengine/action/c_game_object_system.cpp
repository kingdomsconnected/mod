#include "c_game_object_system.h"

#include "i_game_framework.h"

namespace SDK {
    CGameObjectSystem *CGameObjectSystem::GetInstance() {
        auto *framework = IGameFramework::GetInstance();
        if (!framework) {
            return nullptr;
        }

        return static_cast<CGameObjectSystem *>(framework->GetIGameObjectSystem());
    }
} // namespace SDK
