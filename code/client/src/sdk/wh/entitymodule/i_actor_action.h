#pragma once

#include "../framework/i_action.h"

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The actor-action layer every action on an actor's queue shares, above
     * `framework::I_Action` (slots 0-17). Slot meanings are proven on the
     * ladder, jump, crouch, mount and shooting vtables, which all share it.
     */
    class I_ActorAction: public framework::I_Action {
      public:
        ~I_ActorAction() override = default;

        virtual void unk_fn0018() = 0;
        /** The queue slot the action runs in; `C_Action::SetSlot` (0x181A72920). */
        virtual void SetSlot(std::int32_t slot) = 0; // slot 19, +0x98
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        /** The Mannequin fragment the action plays, a global InitializeActorActionFragmentIds (0x1812BA3FC) resolves. */
        virtual std::uint32_t GetFragmentId() const = 0; // slot 26, +0xD0

        // Slot 27 is a thunk to C_Action::SetState; unnamed so
        // framework::C_Action's non-virtual SetState does not override it.
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        /** The action's movement pass (C_ActorActionJump::UpdateMovement, dead's submits the character move request). */
        virtual void UpdateMovement(void *context) = 0; // slot 29, +0xE8
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        /** Another action is taking this one's slot: the shared handler finishes it (slot 35) and drops the replacement's reference. */
        virtual bool OnReplacedBy(I_Action **replacement) = 0; // slot 32, +0x100; a by-value _smart_ptr the callee releases
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        /** `C_Action::RequestFinish`. */
        virtual void RequestFinish(bool a, bool b) = 0; // slot 35, +0x118
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        /** Starts the action (C_ActorActionLadder::Enter, C_ActorActionJump::StartJumpAction180CF7F98). */
        virtual bool Enter(void *context) = 0; // slot 38, +0x130
        virtual void Exit() = 0;               // slot 39, +0x138
        virtual void unk_fn0040() = 0;
        /** Per-frame tick (C_ActorActionLadder::Update, C_ActorActionMount::Update). */
        virtual std::int32_t Update(float frameTime) = 0; // slot 41, +0x148
    };

    static_assert(sizeof(I_ActorAction) == 0x10, "I_ActorAction size mismatch");
} // namespace SDK::wh::entitymodule
