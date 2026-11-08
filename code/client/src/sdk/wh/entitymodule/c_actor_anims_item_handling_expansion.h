#pragma once

#include "../framework/i_action.h"
#include "i_action_actor_expansion.h"
#include "s_actor_anim_request.h"

#include <cstddef>
#include <memory>

namespace SDK::wh::entitymodule {
    class C_ActionActor;

    /**
     * Type-three expansion owned by every C_ActionActor. Slot seven is the
     * native visual-only item-handling action builder; slot seventeen is the
     * gameplay dispatcher and is intentionally left opaque.
     */
    class C_ActorAnimsItemHandlingExpansion: public I_ActionActorExpansion {
      public:
        ~C_ActorAnimsItemHandlingExpansion() override = default;

        /**
         * Explicitly spells the native hidden intrusive-action return storage.
         * The shared request is passed by value and consumed by the callee.
         */
        virtual wh::framework::I_Action **InvokeCreateVisualActionRaw(wh::framework::I_Action **result, std::shared_ptr<S_ActionRequest> request) = 0; // slot 7, +0x38

        virtual std::shared_ptr<S_ActionRequest> *InvokeCreatePickUpRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 8, +0x40, 0x1808CA084
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreatePlaceRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 9, +0x48, 0x1808C9D10
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreateDropRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 10, +0x50, 0x1828B2E68
        /**
         * The animated weapon draw request and its holster twin. Both slots
         * identified by the `_Ref_count_obj2<...>` vftable inside their
         * builders rather than by position.
         */
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreateDrawRequestRaw(std::shared_ptr<S_ActionRequest> *result)    = 0; // slot 11, +0x58
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreateHolsterRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 12, +0x60
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreateSwapWeaponRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 13, +0x68, 0x18183B800
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreatePutItemInHandRequestRaw(std::shared_ptr<S_ActionRequest> *result)      = 0; // slot 14, +0x70
        virtual std::shared_ptr<S_ActionRequest> *InvokeCreatePutItemInInventoryRequestRaw(std::shared_ptr<S_ActionRequest> *result) = 0; // slot 15, +0x78
        virtual void unk_fn0016()                                                                                                    = 0;
        virtual void unk_fn0017()                                                                                                    = 0;

        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        wh::framework::I_Action *CreateVisualAction(const std::shared_ptr<S_ActionRequest> &request) {
            wh::framework::I_Action *result = nullptr;
            InvokeCreateVisualActionRaw(&result, request);
            return result;
        }

        std::shared_ptr<S_ActionRequest> CreatePutItemInHandRequest() {
            std::shared_ptr<S_ActionRequest> result;
            InvokeCreatePutItemInHandRequestRaw(&result);
            return result;
        }

        std::shared_ptr<S_ActionRequest> CreatePutItemInInventoryRequest() {
            std::shared_ptr<S_ActionRequest> result;
            InvokeCreatePutItemInInventoryRequestRaw(&result);
            return result;
        }

      private:
        C_ActionActor *m_pActionActor; // 0x08
        void *m_pItemHandlingFactory;  // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_ActorAnimsItemHandlingExpansion, m_pActionActor) == 0x08, "C_ActorAnimsItemHandlingExpansion::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorAnimsItemHandlingExpansion, m_pItemHandlingFactory) == 0x10, "C_ActorAnimsItemHandlingExpansion::m_pItemHandlingFactory offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorAnimsItemHandlingExpansion) == 0x18, "C_ActorAnimsItemHandlingExpansion size mismatch");
} // namespace SDK::wh::entitymodule
