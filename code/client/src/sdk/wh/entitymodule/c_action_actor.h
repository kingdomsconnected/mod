#pragma once

#include "../framework/i_action.h"
#include "c_action_collection.h"
#include "c_actor_action_rider.h"
#include "i_action_actor_expansion.h"
#include "i_action_actor_spec.h"
#include "s_carry_item_action_params.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_ActorAnimsItemHandlingExpansion;
    class C_ActorShootingExpansion;
    class C_ActorStateExpansion;

    /**
     * An actor's action queue, which every actor owns from construction.
     *
     * `C_Actor`'s constructor builds one against itself and stores it at
     * `+0x280`. Actions are posted through slot 4, proven twice -- by the
     * mount path and by the ride-state path, both reaching the queue as
     * `*(actor + 640)`.
     */
    class C_ActionActor {
      public:
        virtual ~C_ActionActor()  = default; // slot 0
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;

        /** Posts one action, taking over the reference the handle carries. */
        virtual bool QueueAction(wh::framework::I_Action **action) = 0; // slot 4, +0x20
        /** 0x1804A752C: the action in director slot `slot`, AddRef'd into `result`; null out of range. */
        virtual wh::framework::I_Action **GetActionInSlotRaw(wh::framework::I_Action **result, int slot) = 0; // slot 5, +0x28
        virtual void unk_fn0006()                                  = 0;
        /** 0x180856954: the queued action with this id, AddRef'd into `result`. */
        virtual wh::framework::I_Action **FindActionByIdRaw(wh::framework::I_Action **result, int id) = 0; // slot 7, +0x38
        /** 0x1804A74A4: the first of the 13 slots holding an action of `type` (E_ActorActionType). */
        virtual wh::framework::I_Action **FindActionByTypeRaw(wh::framework::I_Action **result, std::uint8_t type) = 0; // slot 8, +0x40
        /** 0x1808567FC: cancels every slotted action whose priority is at or below `priority`. */
        virtual void CancelActionsUpToPriority(int priority) = 0; // slot 9, +0x48
        virtual void unk_fn0010()                                  = 0;
        virtual void unk_fn0011()                                  = 0;
        virtual void unk_fn0012()                                  = 0;
        /** Returns the native typed request router owned by this queue. */
        virtual I_ActionActorSpec *GetActionActorSpec() = 0; // slot 13, +0x68

        /** Returns a typed actor-action expansion; item handling is type 3. */
        virtual I_ActionActorExpansion *GetExpansion(std::uint8_t type) = 0; // slot 14, +0x70

        /**
         * Returns the item-handling expansion, constructing and attaching it
         * through the engine's registered expansion factory when necessary.
         * Unlike GetExpansion(3), this is not lookup-only.
         */
        C_ActorAnimsItemHandlingExpansion *GetItemHandlingExpansion();

        /** Native factory-backed lookup. Use before equipping a remote body. */
        C_ActorShootingExpansion *GetShootingExpansion();

        /**
         * Returns the whole-body state expansion -- jumping, crouching,
         * unconsciousness, ladders, carts -- constructing it through the
         * engine's registered factory when the actor has none yet.
         */
        C_ActorStateExpansion *GetActorStateExpansion();

        /** Lookup only: safe while capturing state from the local body. */
        C_ActorShootingExpansion *FindShootingExpansion();

        /**
         * The ride this queue is running, or null when it is running none.
         *
         * Borrowed: the native releases its reference before returning, and
         * the action survives only while the queue still holds one. Read
         * through it inside the call and keep nothing.
         */
        C_ActorActionRider *FindRiderAction();

        /**
         * Builds a `C_ActorActionCarryItem` for `params` and queues it.
         *
         * False when the actor model already holds `E_ActorState::CarryItem`
         * or the queue refused the action. The action enters on the queue's
         * next update and takes `params.item` out of whatever holds it at the
         * pick-up's attach event.
         */
        bool RequestCarryItem(const S_CarryItemActionParams &params);

        C_Actor *GetActor() const {
            return m_pActor;
        }

        /** The actions this actor is holding. Vtable slot 3 is this field. */
        C_ActionCollection *GetActionCollection() const {
            return m_pActionCollection;
        }

        /**
         * Whether this actor is in the middle of an animation shared with
         * another body.
         *
         * The question CryEngine's own cooperative-animation manager answers
         * with `IsActorBusy`, and answered the same way: by looking at what is
         * running now rather than by remembering what started. Both halves of
         * a shared clip are ordinary actions on their actor -- the person
         * takes `syncAnimMaster` and an animal `dogSyncAnimMaster` -- so the
         * queue is the record, and it is the engine that keeps it.
         */
        bool IsInSharedAnimation() const;

        /** What every action factory takes as its first argument. */
        void *GetActionAllocator() const {
            return m_pActionAllocator;
        }

      private:
        // Two further base vptrs and their state; the class has three in all.
        std::byte m_opaque0008[0x20];
        C_Actor *m_pActor;                     // 0x28
        I_ActionActorSpec *m_pActionActorSpec; // 0x30
        std::byte m_opaque0038[0x10];
        C_ActionCollection *m_pActionCollection; // 0x48
        void *m_pActionAllocator;                // 0x50
        std::byte m_opaque0058[0x28];

        static void AssertLayout() {
            static_assert(offsetof(C_ActionActor, m_pActor) == 0x28, "C_ActionActor::m_pActor offset mismatch");
            static_assert(offsetof(C_ActionActor, m_pActionActorSpec) == 0x30, "C_ActionActor::m_pActionActorSpec offset mismatch");
            static_assert(offsetof(C_ActionActor, m_pActionCollection) == 0x48, "C_ActionActor::m_pActionCollection offset mismatch");
            static_assert(offsetof(C_ActionActor, m_pActionAllocator) == 0x50, "C_ActionActor::m_pActionAllocator offset mismatch");
        }
    };

    static_assert(sizeof(C_ActionActor) == 0x80, "C_ActionActor size mismatch");
} // namespace SDK::wh::entitymodule
