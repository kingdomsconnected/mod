#pragma once

#include "../framework/i_action.h"

#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    struct S_ActionRequest;

    // RTTI and the abstract table at 0x183A3A778 prove this eight-slot base.
    class I_ActionActorExpansion {
      public:
        virtual ~I_ActionActorExpansion()                                                                                                         = default;
        virtual std::uint8_t GetExpansionType() const                                                                                             = 0;
        /** Per-frame update (C_ActorStateExpansion::UpdateCrouchAction, C_ActorShootingExpansion::UpdatePlayerChargeState). */
        virtual void Update() = 0;
        /** Second per-frame pass (C_ActorShootingExpansion::ProcessPendingReload). */
        virtual void PostUpdate() = 0;
        /** Releases what the expansion owns (C_ActorAnimsExpansion::Shutdown 0x180FC1E24). */
        virtual void Shutdown() = 0;
        virtual void unk_fn0005()                                                                                                                 = 0;
        virtual void unk_fn0006()                                                                                                                 = 0;
        virtual wh::framework::I_Action **InvokeCreateVisualActionRaw(wh::framework::I_Action **result, std::shared_ptr<S_ActionRequest> request) = 0;
    };

    static_assert(sizeof(I_ActionActorExpansion) == 0x8, "I_ActionActorExpansion size mismatch");
} // namespace SDK::wh::entitymodule
