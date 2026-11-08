#pragma once

#include "../../patterns.h"
#include "../framework/wuid.h"
#include "c_intelligent_object.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule {
    /**
     * The registry of every brain-carrying XGenAI object, by WUID.
     *
     * A singleton (`g_pIntelligentObjectManager` 0x1854930D0) created by
     * `CreateIntelligentObjectManager` (0x18197934C). The lookup is the
     * inherited `C_ObjectManager<C_IntelligentObject, ...>::FindObject`
     * (0x18070DB90), the one the brain-variable script binds make.
     */
    class C_IntelligentObjectManager {
      public:
        /** Null until the AI module has created it. */
        static C_IntelligentObjectManager *GetInstance() {
            return *reinterpret_cast<C_IntelligentObjectManager **>(gPatterns.xgenaimodule__IntelligentObjectManager);
        }

        /** The object registered under `wuid`, or null. */
        C_IntelligentObject *FindObject(const framework::WUID &wuid) const {
            return hook::this_call<C_IntelligentObject *>(gPatterns.C_IntelligentObjectManager__FindObject, this, &wuid);
        }

        /** The named variable of the brain behind `wuid`, or null when the object, its brain or the variable is missing. */
        C_Variable *FindBrainVariable(const framework::WUID &wuid, const char *name) const {
            const C_IntelligentObject *object = FindObject(wuid);
            C_AIBrain *brain                  = object != nullptr ? object->GetBrain() : nullptr;
            if (brain == nullptr) {
                return nullptr;
            }
            const ScopedCryString key(name);
            return brain->GetVariables()->FindVariable(key, true);
        }
    };
} // namespace SDK::wh::xgenaimodule
