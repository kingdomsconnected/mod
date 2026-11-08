#include "c_action_actor.h"
#include "c_actor_shooting_expansion.h"
#include "e_actor_action_type.h"
#include "c_actor_state_expansion.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_ActorShootingExpansion *C_ActionActor::GetShootingExpansion() {
        return hook::this_call<C_ActorShootingExpansion *>(gPatterns.C_ActionActor__GetShootingExpansion, this);
    }

    C_ActorShootingExpansion *C_ActionActor::FindShootingExpansion() {
        return static_cast<C_ActorShootingExpansion *>(GetExpansion(4));
    }

    C_ActorStateExpansion *C_ActionActor::GetActorStateExpansion() {
        return hook::this_call<C_ActorStateExpansion *>(gPatterns.C_ActionActor__GetActorStateExpansion, this);
    }

    C_ActorActionRider *C_ActionActor::FindRiderAction() {
        return hook::this_call<C_ActorActionRider *>(gPatterns.C_ActionActor__FindRiderAction, this);
    }

    bool C_ActionActor::RequestCarryItem(const S_CarryItemActionParams &params) {
        return hook::this_call<bool>(gPatterns.C_ActionActor__RequestCarryItem, this, &params);
    }

    bool C_ActionActor::IsInSharedAnimation() const {
        const C_ActionCollection *actions = GetActionCollection();
        return actions->FindActionOfType(E_ActorActionType::SyncAnimMaster) != nullptr || actions->FindActionOfType(E_ActorActionType::DogSyncAnimMaster) != nullptr;
    }

    C_ActorAnimsItemHandlingExpansion *C_ActionActor::GetItemHandlingExpansion() {
        return hook::this_call<C_ActorAnimsItemHandlingExpansion *>(gPatterns.C_ActionActor__GetItemHandlingExpansion, this);
    }
} // namespace SDK::wh::entitymodule
