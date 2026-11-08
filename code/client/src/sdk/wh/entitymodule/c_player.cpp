#include "c_player.h"

#include "../../cryengine/action/i_game_framework.h"

namespace SDK::wh::entitymodule {
    C_Player *C_Player::GetInstance() {
        auto *framework = SDK::IGameFramework::GetInstance();
        if (framework == nullptr) {
            return nullptr;
        }

        auto *actor = framework->GetClientActor();
        if (actor == nullptr) {
            return nullptr;
        }

        return static_cast<C_Player *>(actor);
    }
} // namespace SDK::wh::entitymodule
