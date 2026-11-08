#include "c_npc_manager.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule {
    C_NPCManager::LocalPlayerBinding C_NPCManager::CaptureLocalPlayerBinding() const {
        return {m_pLocalPlayerEntity, m_pLocalPlayerNPC, *reinterpret_cast<C_NPC **>(gPatterns.C_NPCManager__LocalPlayerNPCCache)};
    }

    void C_NPCManager::RestoreLocalPlayerBinding(const LocalPlayerBinding &binding) {
        m_pLocalPlayerEntity                                                     = binding.entity;
        m_pLocalPlayerNPC                                                        = binding.npc;
        *reinterpret_cast<C_NPC **>(gPatterns.C_NPCManager__LocalPlayerNPCCache) = binding.cachedNPC;
    }
    C_NPCManager *C_NPCManager::GetInstance() {
        return *reinterpret_cast<C_NPCManager **>(gPatterns.C_NPCManager__Instance);
    }

    void *BuildBrainForGuid(const CryGUID &brain, C_NPC *npc) {
        // A free function -- `YA` -- taking the template registry, the GUID by
        // address, the NPC the brain is being built for, and two zeroed
        // selectors the record initialiser also passes.
        return hook::call<void *>(gPatterns.BuildBrainForGuid, *reinterpret_cast<void **>(gPatterns.AIBrainTemplateRegistry), &brain, npc, 0, 0);
    }

    void *BuildEmptyBrain(C_NPC *npc) {
        const CryGUID unset {0, 0};
        return BuildBrainForGuid(unset, npc);
    }
} // namespace SDK::wh::xgenaimodule
