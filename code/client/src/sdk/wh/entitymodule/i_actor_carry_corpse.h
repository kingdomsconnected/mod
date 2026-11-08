#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * An actor's half of carrying a body on the shoulder -- the carrier's
     * state machine, and the carried body's "being carried" mark.
     *
     * Every actor has one, built on first use by `C_Actor::GetCarryCorpse`
     * into `C_Actor+0x188`. The implementation is `C_ActorCarryCorpse`
     * (0x108 bytes, vtable 0x183A2F438); its state sits at +0x68 (0 none,
     * 1..4 picking up, 5 carrying, 6..8 putting down), the target's entity id
     * at +0x80 and the carrier at +0xD8.
     *
     * Lua reaches it through `C_ScriptBindActor`: `RequestGrabCorpse` is slot
     * 7 and `RequestPutCorpse` slot 10. Neither slot asks anything about the
     * target -- the gate is `CanGrabCorpse` (0x1812EB090), which only the
     * interaction prompt consults -- and the action they post is not gated on
     * the carrier being player-controlled, so a puppet carries the same way.
     */
    class I_ActorCarryCorpse {
      public:
        virtual ~I_ActorCarryCorpse() = default; // slot 0

        virtual std::uint32_t GetCarryPhase() const = 0; // slot 1, +0x08
        virtual bool IsPuttingDown() const          = 0; // slot 2, +0x10

        /** On the shoulder: the pick-up has finished and the put-down not begun. */
        virtual bool IsCarrying() const = 0; // slot 3, +0x18

        virtual bool IsPickingUp() const = 0; // slot 4, +0x20

        /** A body is bound, whatever the phase. */
        virtual bool HasTarget() const = 0; // slot 5, +0x28

        virtual bool HasCarrySideEffect() = 0; // slot 6, +0x30

        /**
         * Posts the carry action with the pick-up clip, as the grab prompt
         * does. Zero when the actor's state expansion refused the action.
         */
        virtual std::uint32_t RequestGrab(EntityId targetEntityId) = 0; // slot 7, +0x38

        /**
         * The same action with its instant flag: the pick-up clip is skipped
         * and the body goes straight onto the shoulder.
         */
        virtual std::uint32_t RequestGrabInstant(EntityId targetEntityId) = 0; // slot 8, +0x40

        virtual bool unk_fn0009(EntityId targetEntityId) = 0;

        /** The animated put-down, as the drop key does. */
        virtual bool RequestPutDown() = 0; // slot 10, +0x50

        /**
         * No put-down clip: the body is moved to the carrier's feet and the
         * carry action stopped. The body is released in that action's Exit,
         * a queue update later, not by this call.
         */
        virtual bool RequestPutDownImmediate() = 0; // slot 11, +0x58

        virtual void Interrupt(bool flag) = 0; // slot 12, +0x60

        /**
         * Stops the running carry action through its queue (the action's slot
         * 35, 0x18194B504, then 0x1809CD780); its Exit is what unbinds the body.
         */
        virtual void StopCarryAction() = 0; // slot 13, +0x68

        virtual EntityId GetTargetEntityId() const = 0; // slot 14, +0x70

        /**
         * Unbinds the carried body now: its physics state, the attachment and
         * the carrier's carry states, exactly as the carry action's Exit does.
         * For a body about to be destroyed, which cannot wait for that Exit.
         * It also forgets the running action without stopping it, so call
         * StopCarryAction first; that action's own Exit then finds nothing
         * bound.
         */
        void EndCarrying();
    };
} // namespace SDK::wh::entitymodule
