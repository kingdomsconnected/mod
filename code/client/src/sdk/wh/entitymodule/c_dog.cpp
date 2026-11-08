#include "c_dog.h"

#include "c_action_actor.h"
#include "c_dog_external_action_sync_anim_master.h"

namespace SDK::wh::entitymodule {
    bool C_Dog::Bond(EntityId partnerEntityId, std::uint32_t variant) {
        C_ActionActor *queue                      = GetActionActor();
        C_DogExternalActionSyncAnimMaster *action = C_DogExternalActionSyncAnimMaster::Create(queue);
        if (action == nullptr) {
            return false;
        }

        action->Configure(partnerEntityId, GetDogBondClipId(this), GetDogBondPartnerClipId(), variant);

        // Queueing takes a reference of its own, so the one the factory handed
        // back is still ours to drop -- which is what the native does too.
        wh::framework::I_Action *queued = action;
        queued->AddRef();
        const bool accepted = queue->QueueAction(&queued);

        action->Release();
        return accepted;
    }
} // namespace SDK::wh::entitymodule
