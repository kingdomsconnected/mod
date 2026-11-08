#include "i_ai_object.h"

#include "i_entity.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    namespace {
        /**
         * Size of the AIObjectParams CScriptBind_Action::RegisterWithAI builds
         * on its stack. Zeroing it is enough for the removal path: the engine
         * reads only the type at +0x3A0 before returning, and constructs
         * nothing that would need tearing down.
         */
        constexpr std::size_t kAIObjectParamsSize = 960;
    } // namespace

    IAIObject *GetEntityAIObject(IEntity *entity) {
        return hook::this_call<IAIObject *>(gPatterns.IEntity__GetAIObject, entity);
    }

    bool UnregisterEntityFromAI(IEntity *entity) {
        alignas(16) std::uint8_t params[kAIObjectParamsSize] {};

        entity->RegisterInAISystem(params);
        return GetEntityAIObject(entity) == nullptr;
    }
} // namespace SDK
