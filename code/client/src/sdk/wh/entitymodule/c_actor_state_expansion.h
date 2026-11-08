#pragma once

#include "../../types.h"
#include "e_cart_horse_slot.h"
#include "i_action_actor_expansion.h"
#include "s_actor_action_cart_params.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>

namespace SDK::wh::entitymodule {
    class C_ActionActor;

    /**
     * Expansion type 1: the whole-body states an actor can be put into that
     * are not shooting, not item handling and not riding -- jumping, going
     * unconscious, ragdolling, climbing a ladder, riding a cart, crouching.
     *
     * Its primary vtable at 0x183B49608 has exactly 33 slots and RequestJump
     * is the last; the qword after it belongs to unrelated RTTI. Only the
     * slots this mod calls carry a signature -- several of the rest return a
     * `_smart_ptr` by value, a hidden-argument return nothing here needs.
     */
    class C_ActorStateExpansion: public I_ActionActorExpansion {
      public:
        /**
         * Builds actor action type 14 and posts it. Not gated on the body
         * being player-controlled, so a puppet is put out the same way.
         *
         * The action's entry is what makes the body limp: it asks the ragdoll
         * manager for a deferred ragdoll unless one is already running.
         */
        virtual bool RequestUnconsciousAction() = 0; // slot 8, +0x40

        /**
         * Ends the unconscious action, if one is running. Not the same as
         * waking the body up: the ragdoll it asked for outlives it, and
         * getting off the floor is `C_RagdollManager::RequestStandUp`.
         */
        virtual void StopUnconsciousAction() = 0; // slot 9, +0x48

        virtual void unk_fn0010() = 0;

        /**
         * Reached by `C_RagdollManager::RequestStandUp` (0x1828792D0) after its
         * gates. Call the manager's instead: this one asks nothing about
         * whether the body is in a ragdoll at all.
         */
        virtual void StandUp(std::uint32_t mode) = 0; // slot 11, +0x58

        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;

        /**
         * Builds a `C_ActorActionCart` from `params` and queues it (0x18289FAB8).
         * `C_CartPlayerControl::MountPlayer` is one caller and the game's NPC
         * seat operation the other; nothing here checks who the body is. The
         * seat is judged on the action's `Enter`, by `C_Cart::OccupyPosition`,
         * so true means queued rather than seated.
         */
        virtual bool RequestCartAction(const S_ActorActionCartParams &params) = 0; // slot 17, +0x88

        virtual void StopCartAction() = 0; // slot 18, +0x90

        /**
         * Gets the body off whatever cart it sits in (0x182895E74): takes the
         * action in the queue's slot 0 and, when it is a cart stay, asks it
         * for the climb down -- the quick drop with `fast`. What the game's
         * `cart_dismount` key runs, with `fast` false. Nothing happens for a
         * body in no cart. The stay applies it at once while the body is
         * visible, and from its next update otherwise.
         */
        virtual void RequestCartDismount(bool fast) = 0; // slot 19, +0x98

        /**
         * Puts this horse in `cartEntityId`'s shafts at `slot` (0x18289FBF8):
         * builds `C_ActorActionHorseInCart` and queues it. Its `Enter` takes
         * the slot and hitches the animal to the cart's follower model, and
         * from then on the cart's movement walks it to its harness helper --
         * and the action's update is what ticks the cart's movement at all.
         */
        virtual bool RequestHorseInCartAction(EntityId cartEntityId, E_CartHorseSlot::Type slot) = 0; // slot 20, +0xA0
        // 0x181613354: stops only horseInCart; Exit releases the harness and path.
        virtual void StopHorseInCartAction() = 0; // slot 21, +0xA8
        /** 0x18289AEB4: the shaft slot of a running horseInCart action; false without one. */
        virtual bool TryGetHorseInCartSlot(E_CartHorseSlot::Type *slot) = 0; // slot 22, +0xB0
        /** 0x18289AF08: the seat of a running cart action (`m_params.position`); false without one. */
        virtual bool TryGetCartPosition(E_CartPosition::Type *position) = 0; // slot 23, +0xB8
        /** Writes `C_ActorActionLadder::m_climbInput`; no-op without one. */
        virtual void SetLadderClimbInput(float input) = 0; // slot 24, +0xC0
        /** 0x1828968E8: drops off the ladder (C_ActorActionLadder::DropFromLadder); false without one. */
        virtual bool DropFromLadder() = 0; // slot 25, +0xC8

        /**
         * Leaves the ladder at whichever end is nearer, playing the get-off
         * that belongs to it -- `DropFromLadder` is the one that falls. Not
         * idempotent: it also stops the action and places the body, so it
         * must be called once rather than held.
         */
        virtual void ExitLadderNearPosition(const Vec3 &position) = 0; // slot 26, +0xD0
        /** 0x18289BC8C: 0 with no ladder action, otherwise the climb phase it is in (1-3). */
        virtual std::uint8_t GetLadderState() const = 0; // slot 27, +0xD8

        /**
         * Hands the queued jump action its impulse. The engine calls it from
         * `CProceduralClipJump::OnEnter`, so a jump started through
         * RequestJump leaves the ground without anything here pushing it.
         */
        virtual void TriggerJumpImpulse() = 0; // slot 28, +0xE0

        virtual void SetCrouchRequested(bool requested, bool immediate) = 0; // slot 29, +0xE8
        virtual bool IsCrouchRequested() const                          = 0; // slot 30, +0xF0
        virtual bool unk_fn0031()                                       = 0;

        /**
         * Asks the engine for a jump, as the local player's own ground
         * movement does.
         *
         * Answers the game's jump gate first -- not in the blocked model
         * state, not down, able to stand if crouched, and the soul holds the
         * stamina the jump costs -- then builds actor action type 9 and posts
         * it. False is the engine refusing; no jump was started. It does not
         * move the body: the impulse arrives from the clip.
         */
        virtual bool RequestJump() = 0; // slot 32, +0x100

        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /**
         * Builds actor action type 0x22 and posts it, as the ladder's own Lua
         * does through `C_Human::GrabOnLadder`. Not gated on the body being
         * player-controlled, so a puppet grabs a ladder the same way.
         *
         * Not a vtable slot -- the table at 0x183B49608 has 33 and this is
         * not among them -- so it is called by address.
         */
        bool RequestLadderAction(EntityId ladderEntityId) {
            return hook::this_call<bool>(gPatterns.C_ActorStateExpansion__RequestLadderAction, this, ladderEntityId);
        }

        /**
         * Builds actor action type 15 -- dead -- and posts it, as
         * `C_Actor::HandleDeath` does.
         *
         * `freshDeath` false settles the body straight into the dead pose:
         * it clears one Mannequin tag, sets another and marks the queued
         * action. That is what the two engine callers restoring an
         * already-dead body pass (`C_Actor::SetPutAway` at 0x18087A9A5 and
         * 0x180B24783, both after the actor reports dead); true is what
         * `HandleDeath` forwards for a body dying now.
         *
         * Not a vtable slot -- the table at 0x183B49608 has 33 and this is not
         * among them. Returns whether the action was created.
         */
        bool RequestDeadAction(bool freshDeath) {
            return hook::this_call<bool>(gPatterns.C_ActorStateExpansion__RequestDeadAction, this, freshDeath);
        }

      private:
        C_ActionActor *m_pActionActor; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_ActorStateExpansion, m_pActionActor) == 0x08, "C_ActorStateExpansion::m_pActionActor offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
