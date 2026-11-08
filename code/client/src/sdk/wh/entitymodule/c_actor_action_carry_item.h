#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../../cryengine/action/i_mannequin_action.h"
#include "../framework/c_action.h"
#include "../framework/wuid.h"
#include "c_action_actor.h"
#include "i_actor_action.h"
#include "s_carry_item_action_params.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The action that holds an item in an actor's arms, from the pick-up to
     * the end of the put-down -- a firewood armful, a basket, a sack.
     *
     * Built by `C_ActionActor::RequestCarryItem` (0x181391738), which a pile's
     * Lua `PickUp` and a carryable item's both end in. Nothing on the way is
     * gated on the player: a puppet's `C_Human` runs it exactly as the local
     * player does, and only the input maps and the head-turn cap are player
     * work.
     *
     * The walk is not this action's doing. `CompleteItemPickup` (0x180D7C2D4)
     * puts the item in a hand, and `C_ItemAttachmentManager::
     * UpdateHandManipulationTags` (0x1809416E8) turns the hand's item into
     * the `ManTypeRight`/`ManTypeLeft` tag groups, which is what selects the
     * carrying variants of every locomotion fragment.
     *
     * The item never reaches the carrier's inventory: the action is itself an
     * item holder (the interface at +0x80) and the transfer names it as the
     * destination. A put-down drops the item back into the world through
     * `DropHandledItemById`; `Exit` does the same for anything still in hand.
     *
     * The factory allocates 0xD8 bytes (0x1813917BC, constructor 0x181391884):
     * the framework action to +0x50, the parameters, then the `C_ActorObject`
     * base whose queue at +0x70 is the one the action reaches its carrier by.
     */
    // RTTI (base ctor 0x1813919B4): C_ActorAction<I_ActorAction, C_ParamAction<S_CarryItemActionParams, C_Action<I_ActorAction>>>.
    class C_ActorActionCarryItem: public framework::C_ParamAction<S_CarryItemActionParams, framework::C_Action<I_ActorAction>> {
      public:
        /**
         * The phase byte at +0x90, written only through `SetCarryPhase`
         * (0x1810840FC). Entering `Carrying` clears the actor's allow-run byte
         * (`C_Actor+0x40A`) and leaving it puts the saved value back.
         */
        struct E_Phase {
            enum Type : std::uint8_t {
                New         = 0,
                PickingUp   = 1,
                Interrupted = 2,
                Carrying    = 3,
                PuttingDown = 4,
                Dropped     = 5,
            };
        };

        /** The item this carry is for, as the request named it. */
        framework::WUID GetItemId() const {
            return GetParams().item;
        }

        E_Phase::Type GetPhase() const {
            return m_phase;
        }

        /** The queue this was posted to, whose owner is therefore the carrier. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /**
         * The holder the picked-up item is transferred into, as
         * `S_ItemTransferDescriptor::GetDestinationHolder` names it.
         */
        const void *GetItemHolder() const {
            return &m_itemHolder;
        }

        /**
         * Moves the carry to another phase through its own setter
         * (`SetCarryPhase`, 0x1810840FC), which does the bookkeeping that
         * entering and leaving `Carrying` needs.
         *
         * Set to anything but `New` before the action enters, it is what
         * makes the pick-up instant: `Enter` (0x181083478) queues the
         * animated pick-up only for a `New` carry, and for any other runs
         * `CompleteItemPickup` on the spot -- the item goes straight into the
         * hand, wherever it was held, with no clip and no pickable on the
         * ground.
         */
        void SetPhase(E_Phase::Type phase) {
            hook::this_call<void>(gPatterns.C_ActorActionCarryItem__SetCarryPhase, this, phase);
        }

        /**
         * Plays the put-down and drops the item at its drop event. What the
         * game's own `put_item` interaction runs; only meaningful while
         * `Carrying`.
         */
        void BeginPutDownAnimation() {
            hook::this_call<void>(gPatterns.C_ActorActionCarryItem__BeginPutDownAnimation, this);
        }

        /**
         * The fragment the pick-up or put-down last queued, a
         * `C_CallbackAction` both `Begin*Animation`s create here; null before
         * the first.
         */
        SDK::IAction *GetFragmentAction() const {
            return m_pFragmentAction;
        }

        /**
         * Ends the action through its queue (`C_Action::Finish`, 0x1806C3A90,
         * reading the queue at +0x40 and the slot from `GetSlot`). `Exit` then
         * drops whatever is still in hand, without a clip. Passes the result
         * the put-down's own finish does (`OnPutDownAnimationFinished`,
         * 0x18287C288); 1 is the failure its pick-up reports.
         */
        void Finish() {
            hook::this_call<void>(gPatterns.C_Action__Finish, this, 2U);
        }

      private:
        std::byte m_actorObject[0x8];     // 0x68, C_ActorObject vtable
        C_ActionActor *m_pActionActor;   // 0x70
        std::uint16_t m_state0078;       // 0x78, base ctor 0x1813919B4 zeroes
        std::byte m_padding007A[0x2];
        std::uint32_t m_state007C;       // 0x7C, base ctor zeroes
        std::byte m_itemHolder[0x8];     // 0x80, the item-holder interface's vtable
        std::byte m_interface0088[0x8];  // 0x88, a further interface vtable ctor 0x181391884 installs
        E_Phase::Type m_phase;           // 0x90
        std::byte m_padding0091[0x7];
        void *m_value0098;               // 0x98, ctor zeroes
        std::uint32_t m_attachEventId;   // 0xA0, CRC of "Attach" (lower-cased)
        std::uint32_t m_detachEventId;   // 0xA4, CRC of "Detach" (lower-cased)
        SDK::IAction *m_pFragmentAction; // 0xA8, ctor zeroes; the queued pick-up/put-down fragment
        void *m_values00B0[2];           // 0xB0, ctor zeroes
        std::uint32_t m_state00C0;       // 0xC0, ctor zeroes
        std::byte m_padding00C4[0x4];
        void *m_value00C8;               // 0xC8, ctor zeroes
        std::uint32_t m_state00D0;       // 0xD0, ctor zeroes
        std::uint16_t m_state00D4;       // 0xD4, ctor zeroes
        std::byte m_padding00D6[0x2];

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionCarryItem, m_actorObject) == 0x68, "C_ActorActionCarryItem::m_actorObject offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_pActionActor) == 0x70, "C_ActorActionCarryItem::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_itemHolder) == 0x80, "C_ActorActionCarryItem::m_itemHolder offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_phase) == 0x90, "C_ActorActionCarryItem::m_phase offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_attachEventId) == 0xA0, "C_ActorActionCarryItem::m_attachEventId offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_pFragmentAction) == 0xA8, "C_ActorActionCarryItem::m_pFragmentAction offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_values00B0) == 0xB0, "C_ActorActionCarryItem::m_values00B0 offset mismatch");
            static_assert(offsetof(C_ActorActionCarryItem, m_state00D4) == 0xD4, "C_ActorActionCarryItem::m_state00D4 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionCarryItem) == 0xD8, "C_ActorActionCarryItem size mismatch");
} // namespace SDK::wh::entitymodule
