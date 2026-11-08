#include "i_actor_system.h"

#include "i_game_framework.h"

namespace SDK {
    IActorSystem *IActorSystem::GetInstance() {
        const auto gameFramework = IGameFramework::GetInstance();
        return gameFramework != nullptr ? gameFramework->GetIActorSystem()
                                        : nullptr;
    }
} // namespace SDK
