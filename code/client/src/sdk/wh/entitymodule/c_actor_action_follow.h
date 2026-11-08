#pragma once

#include "../framework/i_action.h"

namespace SDK::wh::entitymodule {
    /**
     * The action that walks or rides the local player after a target --
     * actor action type `E_ActorActionType::Follow`, queued by
     * `C_ChatFollowManager::StartFollowing`.
     *
     * Only the vtable is typed. Slot 35 is shared by every
     * `C_Action<I_ActorAction>`, so it is proven on all 41 of their primary
     * vtables, this one included.
     */
    class C_ActorActionFollow: public wh::framework::I_Action {
      public:
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;

        /**
         * Stores both flags at `+0x68`/`+0x69` and finishes the action through
         * its queue (`C_Action::Finish(this, 2)`), so `Exit` runs and drops
         * the movement adapter. Slot 35, `+0x118`.
         */
        virtual void RequestFinish(bool first, bool second) = 0; // slot 35, +0x118
    };
} // namespace SDK::wh::entitymodule
