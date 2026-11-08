#pragma once

#include "../../types.h"
#include "c_actor_action_item_handling_anim.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    struct S_ButcheringActionParams {
        SDK::EntityId target;
    };
    static_assert(sizeof(S_ButcheringActionParams) == 0x04, "S_ButcheringActionParams size mismatch");
    static_assert(offsetof(S_ButcheringActionParams, target) == 0x00, "S_ButcheringActionParams target mismatch");

    /** Ctor 0x1828938D8: the small parameter action's real base prefix. */
    template <>
    class C_ActorAction<I_ActorAction, framework::C_ParamAction<S_ButcheringActionParams, framework::C_Action<I_ActorAction>>>
        : public framework::C_ParamAction<S_ButcheringActionParams, framework::C_Action<I_ActorAction>>
        , public C_ActorObject {
      private:
        std::uint16_t m_state0068;
        std::byte m_opaque006A[0x02];
        std::uint32_t m_state006C;
        static void AssertLayout() {
            static_assert(offsetof(C_ActorAction, m_state0068) == 0x68, "butchering action base state mismatch");
            static_assert(offsetof(C_ActorAction, m_state006C) == 0x6C, "butchering action base state mismatch");
        }
    };
    static_assert(sizeof(C_ActorParamAction<S_ButcheringActionParams, I_ActorAction>) == 0x70, "butchering parameter action size mismatch");

    /** RTTI/ctor 0x182893B18; factory 0x1828C267C allocates 0x90. */
    class C_ActorActionButchering: public C_ActorParamAction<S_ButcheringActionParams, I_ActorAction> {
      public:
        SDK::EntityId GetTargetEntityId() const {
            return GetParams().target;
        }
        void MarkCompleted() {
            m_completed = true;
        }
        /** Finish reason 1 runs Exit with WasCancelled set, without harvesting. */
        void Cancel() {
            hook::this_call<void>(gPatterns.C_Action__Finish, this, 1U);
        }

      private:
        std::uint32_t m_faderEventHash; // 0x70, ctor hashes Fader
        bool m_completed;               // 0x74, Complete writes before resolving its target
        std::byte m_opaque0075[0x1B];
        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionButchering, m_faderEventHash) == 0x70, "C_ActorActionButchering fader event mismatch");
            static_assert(offsetof(C_ActorActionButchering, m_completed) == 0x74, "C_ActorActionButchering completion mismatch");
        }
    };
    static_assert(sizeof(C_ActorActionButchering) == 0x90, "C_ActorActionButchering size mismatch");
} // namespace SDK::wh::entitymodule
