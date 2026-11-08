#include "c_dog_external_action_sync_anim_master.h"

#include "../../patterns.h"
#include "c_action_actor.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_DogExternalActionSyncAnimMaster *C_DogExternalActionSyncAnimMaster::Create(C_ActionActor *dogQueue) {
        // The native takes the storage slot by pointer and the dog's queue in
        // RDX, exactly as `C_Dog::Bond` calls it at `0x1829DF469`.
        C_DogExternalActionSyncAnimMaster *action = nullptr;
        hook::call<void>(gPatterns.C_DogExternalActionSyncAnimMaster__Construct, &action, dogQueue);
        return action;
    }

    void C_DogExternalActionSyncAnimMaster::Configure(EntityId partnerEntityId, std::uint32_t dogClipId, std::uint32_t sharedClipId, std::uint32_t variant) {
        m_partnerEntityId = partnerEntityId;
        m_dogClipId       = dogClipId;
        m_reserved0058    = 0;
        m_reserved005C    = 0;
        m_reserved0060    = 0;
        m_reserved0064    = 0;
        m_reserved0068    = 0;
        m_sharedClipId    = sharedClipId;
        m_reserved0070    = 0;
        m_reserved0074    = 0;
        m_reserved0078    = 0;
        m_reserved007C    = 0;
        m_reserved0080    = 0;
        m_variant         = variant;
        m_flag0088        = 1;
        m_flag0089        = 1;
        m_flag008A        = 1;
        m_reserved008B    = 0;
        m_reserved008C    = 0;
        m_mode0090        = 3;

        // Last, as the native does it: the action is only ready once the block
        // above is filled.
        hook::this_call<void>(gPatterns.C_Action__SetState, this, 2u);
    }

    std::uint32_t GetDogBondClipId(C_Dog *dog) {
        return hook::this_call<std::uint32_t>(gPatterns.GetDogBondClipId, dog);
    }

    std::uint32_t GetDogBondPartnerClipId() {
        return hook::call<std::uint32_t>(gPatterns.GetDogBondPartnerClipId);
    }
} // namespace SDK::wh::entitymodule
