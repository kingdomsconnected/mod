#pragma once

#include "../../cryengine/system/i_multithread_reference_target.h"

#include <cstdint>

namespace SDK::wh::framework {
    /**
     * A queued actor action, reference counted: the factory hands back one
     * reference, the queue takes its own, and the caller drops the factory's.
     */
    struct I_Action: SDK::_i_multithread_reference_target<int> {
        ~I_Action() override    = default; // slot 0
        void AddRef() override  = 0;       // slot 1, +0x08
        void Release() override = 0;       // slot 2, +0x10


        /**
         * Whether the action is running. Slot 4, `+0x20`: what
         * `C_ActionActor::InsertActionSlot` (0x1809CC3D4) asks of the action
         * already in a slot before it weighs replacing it -- an inactive one is
         * simply overwritten.
         */
        virtual bool IsActive() = 0; // slot 4, +0x20

        /** Whether the action has run its Exit (`C_Action::HasExited`, 0x181A75490). */
        virtual bool HasExited() const = 0; // slot 5, +0x28
        /** Whether it ended by cancellation rather than finishing (`C_Action::WasCancelled`, 0x181A7D930). */
        virtual bool WasCancelled() const = 0; // slot 6, +0x30
        /** Whether `other` may take this action's slot (`C_CombatActorAction::CanBeReplacedBy`, 0x1814962EC, compares states). */
        virtual bool CanBeReplacedBy(I_Action **other) = 0; // slot 7, +0x38
        /**
         * Whether `other` may run alongside this action. Jump (0x18137FEAC)
         * admits its own land (type 10); crouch (0x18137FF20) a fixed set of
         * types; most actions answer the shared false stub.
         */
        virtual bool CanCoexistWith(I_Action **other) = 0; // slot 8, +0x40
        /** Hands the action the list it now runs alongside: jump keeps its land (0x1819A95FC), crouch records them all (0x18287C500). */
        virtual void SetConcurrentActions(const void *actions) = 0; // slot 9, +0x48; `actions` is a begin/end pair of I_Action smart pointers
        /** Drops what slot 9 recorded (crouch 0x18287C2BC); a no-op elsewhere. */
        virtual void ClearConcurrentActions() = 0; // slot 10, +0x50
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;

        /**
         * What kind of action this is; see `E_ActorActionType`.
         *
         * Slot 14, `+0x70`. `C_ActionActor::FindRiderActionPredicate`
         * (`0x180856E68`) is the proof: the whole of its body is this call
         * compared against the type its caller asked for.
         */
        virtual std::uint8_t GetActionType() = 0; // slot 14, +0x70

        /**
         * The action's state (`+0x38`), slot 15, `+0x78`. It doubles as the
         * action's standing in its slot: `InsertActionSlot` lets a new action
         * replace a running one only when the new one's state is the higher
         * (`C_CombatActorAction::CanBeReplacedBy`, 0x1814962EC).
         */
        virtual std::uint32_t GetState() const = 0; // slot 15, +0x78

        /** Which slot of its queue the action runs in (`+0x3C`), slot 16, `+0x80`. */
        virtual std::int32_t GetSlot() const = 0; // slot 16, +0x80

        /** The action's id (`+0x48`), slot 17, `+0x88` -- what `C_ActionActor::FindActionById` (0x180856954) compares. */
        virtual std::uint32_t GetId() const = 0; // slot 17, +0x88
    };

    static_assert(sizeof(I_Action) == 0x10, "I_Action includes the native reference-counted base");
} // namespace SDK::wh::framework
