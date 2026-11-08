#pragma once

#include "../framework/c_action.h"
#include "c_action_actor.h"
#include "c_actor_object.h"
#include "i_actor_action.h"
#include "s_actor_anim_request.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    /** Built by 0x18039D994 inside every actor animation action's params. */
    struct S_ActorAnimActionDataInstance {
        void *m_value0000;              // 0x00, zeroed
        void *m_value0008;              // 0x08, zeroed
        std::uint32_t m_value0010;      // 0x10, zeroed
        float m_value0014;              // 0x14, set to 1.0
        float m_values0018[3];          // 0x18, zeroed
        std::uint16_t m_state0024;      // 0x24, zeroed
        std::byte m_padding0026[0x2];
        std::uint32_t m_value0028;      // 0x28, zeroed
        std::byte m_padding002C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimActionDataInstance, m_value0014) == 0x14, "S_ActorAnimActionDataInstance::m_value0014 offset mismatch");
            static_assert(offsetof(S_ActorAnimActionDataInstance, m_state0024) == 0x24, "S_ActorAnimActionDataInstance::m_state0024 offset mismatch");
            static_assert(offsetof(S_ActorAnimActionDataInstance, m_value0028) == 0x28, "S_ActorAnimActionDataInstance::m_value0028 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimActionDataInstance) == 0x30, "S_ActorAnimActionDataInstance size mismatch");

    /** Parameters composed at +0x50 in every actor animation action. */
    struct S_ActorActionAnimParams {
        const std::shared_ptr<S_ActionRequest> &GetRequest() const {
            return m_request;
        }

      private:
        S_ActorAnimActionDataInstance m_actionData; // 0x00 / action +0x50
        std::shared_ptr<S_ActionRequest> m_request; // 0x30 / action +0x80

        static void AssertLayout() {
            static_assert(offsetof(S_ActorActionAnimParams, m_actionData) == 0x00, "S_ActorActionAnimParams::m_actionData offset mismatch");
            static_assert(offsetof(S_ActorActionAnimParams, m_request) == 0x30, "S_ActorActionAnimParams::m_request offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorActionAnimParams) == 0x40, "S_ActorActionAnimParams size mismatch");

    using C_ActorFrameworkAction      = framework::C_Action<I_ActorAction>;
    using C_ActorFrameworkParamAction = framework::C_ParamAction<S_ActorActionAnimParams, C_ActorFrameworkAction>;

    static_assert(sizeof(C_ActorFrameworkAction) == 0x50, "actor framework C_Action size mismatch");
    static_assert(sizeof(C_ActorFrameworkParamAction) == 0x90, "actor framework C_ParamAction size mismatch");

    template <typename TInterface, typename TParamAction>
    class C_ActorAction
        : public TParamAction
        , public C_ActorObject {
      private:
        // Laid out for the animation params (0x40 at +0x50); base ctor 0x18039D8A8 and C_ActorActionAnim's 0x18039D838.
        std::uint16_t m_state00A0;    // 0xA0, base ctor zeroes
        std::byte m_padding00A2[0x2];
        std::uint32_t m_state00A4;    // 0xA4, base ctor zeroes
        void *m_value00A8;            // 0xA8, zeroed by C_ActorActionAnim's ctor
        std::uint16_t m_state00B0;    // 0xB0, zeroed by C_ActorActionAnim's ctor
        std::byte m_padding00B2[0x6];
    };

    using C_ActorActionAnimBase = C_ActorAction<I_ActorAction, C_ActorFrameworkParamAction>;

    /** Non-animation specialization: ctor 0x1809CDA38, dead-action base. */
    template <>
    class C_ActorAction<I_ActorAction, framework::C_Action<I_ActorAction>>
        : public framework::C_Action<I_ActorAction>
        , public C_ActorObject {
      private:
        std::uint16_t m_state0060;    // 0x60, ctor 0x1809CDA38 zeroes
        std::byte m_padding0062[0x2];
        std::uint32_t m_state0064;    // 0x64, ctor zeroes
    };

    static_assert(sizeof(C_ActorAction<I_ActorAction, framework::C_Action<I_ActorAction>>) == 0x68, "actor action base size mismatch");

    static_assert(sizeof(C_ActorActionAnimBase) == 0xB8, "C_ActorAction animation base size mismatch");

    template <typename TParams, typename TInterface>
    class C_ActorParamAction: public C_ActorAction<TInterface, framework::C_ParamAction<TParams, framework::C_Action<TInterface>>> {};

    template <typename TParams, typename TInterface>
    class C_ActorAnimatedAction: public C_ActorParamAction<TParams, TInterface> {};

    using C_ActorAnimatedAnimAction = C_ActorAnimatedAction<S_ActorActionAnimParams, I_ActorAction>;

    static_assert(sizeof(C_ActorAnimatedAnimAction) == 0xB8, "C_ActorAnimatedAction animation size mismatch");

    class I_SynchronizedActorAnim {
      public:
        virtual ~I_SynchronizedActorAnim() = default;
    };

    static_assert(sizeof(I_SynchronizedActorAnim) == 0x08, "I_SynchronizedActorAnim size mismatch");

    class C_ActorActionAnim
        : public C_ActorAnimatedAnimAction
        , public I_SynchronizedActorAnim {
      private:
        // Ctor 0x18039D838.
        std::uint16_t m_state00C0;    // 0xC0, ctor zeroes
        std::uint8_t m_state00C2;     // 0xC2, ctor zeroes
        std::byte m_padding00C3[0x5];
    };

    static_assert(sizeof(C_ActorActionAnim) == 0xC8, "C_ActorActionAnim size mismatch");

    class C_ActorActionItemHandlingAnim: public C_ActorActionAnim {
      public:
        const std::shared_ptr<S_ActionRequest> &GetAcceptedRequest() const {
            return GetParams().GetRequest();
        }

      private:
        // Factory 0x1808CBEFC.
        void *m_value00C8;            // 0xC8, factory zeroes
        std::uint8_t m_state00D0;     // 0xD0, factory zeroes
        std::byte m_padding00D1[0x7];
    };

    static_assert(sizeof(C_ActorActionItemHandlingAnim) == 0xD8, "C_ActorActionItemHandlingAnim size mismatch");
} // namespace SDK::wh::entitymodule
