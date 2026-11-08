#include <utils/safe_win32.h>

#include "companion_possession.h"

#include "features/player/player.h"
#include "common/module_support.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_soul_group.h"

namespace KCDC::Features::Companion {
    SDK::wh::rpgmodule::C_Soul *ResolvePlayerSoul(std::uint64_t playerNetworkId) {
        auto *player = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(playerNetworkId);
        if (player == nullptr) {
            return nullptr;
        }

        SDK::wh::entitymodule::C_Actor *actor = player->ResolveBody();
        return actor != nullptr ? actor->GetSoul() : nullptr;
    }

    PossessionChange ReconcileMaster(SDK::wh::rpgmodule::C_Soul *animal, SDK::wh::rpgmodule::C_Soul *master) {
        if (animal == nullptr || animal->GetCompanionMaster() == master) {
            return PossessionChange::None;
        }

        // Somebody else's, or somebody's and it should be nobody's.
        ReleaseFromMaster(animal);

        auto *group = master != nullptr ? master->GetSoulGroup() : nullptr;
        if (group == nullptr) {
            return PossessionChange::Released;
        }
        return group->PossessAnimal(animal, true) ? PossessionChange::Possessed : PossessionChange::Refused;
    }

    void ReleaseFromMaster(SDK::wh::rpgmodule::C_Soul *animal) {
        auto *master = animal != nullptr ? animal->GetCompanionMaster() : nullptr;
        auto *group  = master != nullptr ? master->GetSoulGroup() : nullptr;
        if (group != nullptr) {
            group->ReleaseAnimal(animal);
        }
    }
} // namespace KCDC::Features::Companion
