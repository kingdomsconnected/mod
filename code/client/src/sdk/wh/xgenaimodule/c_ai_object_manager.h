#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../framework/wuid.h"
#include "c_ai_object.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule {
    /**
     * XGenAI's registry of every object with a WUID: smart objects, souls,
     * items -- anything the game's scripts and behaviour trees name by WUID.
     *
     * A process-wide singleton (`s_pInstance185481FC8`, created by 0x180BECBAC).
     * Its map lives inside it and is only ever read through the native lookup.
     */
    class C_AIObjectManager {
      public:
        /** Null until the game module that owns it has been created. */
        static C_AIObjectManager *GetInstance() {
            return *reinterpret_cast<C_AIObjectManager **>(gPatterns.C_AIObjectManager__Instance);
        }

        /** The AI object registered under `wuid`, or null. */
        C_AIObject *FindAIObjectByWuid(framework::WUID wuid) const {
            return hook::this_call<C_AIObject *>(gPatterns.C_AIObjectManager__FindAIObjectByWUID, this, &wuid);
        }

        /**
         * The entity a WUID stands for, or 0: the same walk
         * `C_ScriptBindXGenAIModule::GetEntityIdByWUID` makes.
         */
        SDK::EntityId FindEntityIdByWuid(framework::WUID wuid) const {
            const C_AIObject *object           = FindAIObjectByWuid(wuid);
            const I_AIObjectEntityOwner *owner = object != nullptr ? object->GetOwner() : nullptr;
            return owner != nullptr ? owner->GetEntityId() : 0;
        }
    };

    /**
     * The WUID of the entity with that level EntityGUID, or `WUID::kInvalid`.
     *
     * The inverse direction to `C_AIObjectManager::FindEntityIdByWuid`, and
     * the one `C_ScriptBindXGenAIModule::GetMyWUID` takes: a global
     * `unordered_map<EntityGUID, T*>` (`g_pEntityGuidWuidMap185496338`) whose
     * value answers the WUID, read through the native
     * `FindWUIDByEntityGuid18047E638`. A miss answers a pointer to the
     * invalid WUID, never null. The map is created with the game module
     * (0x180E0BBAC), so it is null only before the game has initialised.
     */
    inline framework::WUID FindWuidByEntityGuid(SDK::EntityGUID guid) {
        void *map = *reinterpret_cast<void **>(gPatterns.xgenaimodule__EntityGuidWuidMap);
        if (map == nullptr) {
            return framework::WUID {framework::WUID::kInvalid};
        }
        return *hook::call<const framework::WUID *>(gPatterns.xgenaimodule__FindWUIDByEntityGuid, map, guid);
    }
} // namespace SDK::wh::xgenaimodule
