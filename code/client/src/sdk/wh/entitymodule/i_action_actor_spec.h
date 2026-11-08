#pragma once

#include "s_actor_anim_request.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace SDK::wh::entitymodule {
    class C_ActionActor;

    /** Exact eight-byte result written by C_ActionActorSpec::RequestAction. */
    struct S_ActionRequestResult {
        std::uint8_t status;
        std::byte m_padding0001[0x3];
        std::uint32_t actionId;

        bool Accepted() const {
            return status == 0;
        }
    };

    static_assert(sizeof(S_ActionRequestResult) == 0x8, "S_ActionRequestResult size mismatch");
    static_assert(offsetof(S_ActionRequestResult, status) == 0x0, "S_ActionRequestResult::status offset mismatch");
    static_assert(offsetof(S_ActionRequestResult, actionId) == 0x4, "S_ActionRequestResult::actionId offset mismatch");

    /** Native actor-action request router. */
    class I_ActionActorSpec {
      public:
        virtual ~I_ActionActorSpec() = default; // slot 0

        virtual S_ActionRequestResult *InvokeRequestActionRaw(S_ActionRequestResult *result, std::shared_ptr<S_ActionRequest> request) = 0; // slot 1, +0x08
        virtual void unk_fn0002(std::uint32_t actionId)                                                                                = 0;
        virtual void unk_fn0003(std::uint32_t actionId, bool first, bool second)                                                       = 0;

        S_ActionRequestResult RequestAction(std::shared_ptr<S_ActionRequest> request) {
            S_ActionRequestResult result {};
            InvokeRequestActionRaw(&result, std::move(request));
            return result;
        }
    };

    static_assert(sizeof(I_ActionActorSpec) == 0x8, "I_ActionActorSpec size mismatch");

    /** Concrete request router allocated and owned by C_ActionActor. */
    class C_ActionActorSpec final: public I_ActionActorSpec {
      public:
        S_ActionRequestResult *InvokeRequestActionRaw(S_ActionRequestResult *result, std::shared_ptr<S_ActionRequest> request) override;
        void unk_fn0002(std::uint32_t actionId) override;
        void unk_fn0003(std::uint32_t actionId, bool first, bool second) override;

      private:
        C_ActionActor *m_pOwner; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_ActionActorSpec, m_pOwner) == 0x08, "C_ActionActorSpec::m_pOwner offset mismatch");
        }
    };

    static_assert(sizeof(C_ActionActorSpec) == 0x10, "C_ActionActorSpec size mismatch");
} // namespace SDK::wh::entitymodule
