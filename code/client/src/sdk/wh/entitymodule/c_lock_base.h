#pragma once

#include "../../patterns.h"

#include "../../cryengine/action/i_actor.h"
#include "../../cryengine/scriptsystem/script_arguments_function.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The engine's broadcast list: an array of {context, function} pairs whose
     * entry count is the uint16 at `m_pSubscribers - 4`, plus a re-entrancy
     * cursor pair. An empty signal points at a shared all-0xFF sentinel, whose
     * count word reads 0xFFFF and exits the loop at 0x180875DF8 immediately.
     *
     * RTTI calls the one at `C_Stash+0x80` a `UnsafeOp::CryDeferrable<0>` base,
     * which would imply a vtable at +0x00; the field there is the data array
     * the broadcast walks, so the type name is not claimed.
     */
    struct SignalLayout {
        void *m_pSubscribers;         // 0x00
        std::uint16_t m_cursor0;      // 0x08
        std::uint16_t m_cursor1;      // 0x0A
        std::byte m_padding000C[0x4]; // 0x0C

        /**
         * Calls every subscriber: `wh::framework::C_Signal::Broadcast`
         * (0x180875DF8), what `C_ScriptBindLockBase::ReportLocked`,
         * `ReportUnlocked` and `ReportLockpicked` each reduce to on their own
         * lock-base signal.
         */
        void Broadcast() {
            hook::this_call<void>(gPatterns.C_Signal180875DF8__Broadcast, this);
        }
    };

    static_assert(sizeof(SignalLayout) == 0x10, "SignalLayout size mismatch");
    static_assert(offsetof(SignalLayout, m_pSubscribers) == 0x00, "SignalLayout::m_pSubscribers offset mismatch");
    static_assert(offsetof(SignalLayout, m_cursor0) == 0x08, "SignalLayout::m_cursor0 offset mismatch");
    static_assert(offsetof(SignalLayout, m_cursor1) == 0x0A, "SignalLayout::m_cursor1 offset mismatch");
    static_assert(offsetof(SignalLayout, m_padding000C) == 0x0C, "SignalLayout::m_padding000C offset mismatch");

    class C_Stash;

    /**
     * Base for everything lockable: `C_AnimDoor`, `C_LockpickableEntity` and
     * `C_Stash`, the three the rttr registration at 0x180158AF0 names.
     *
     * It carries no lock state. Its only members are the three signals below;
     * the lock lives on the entity's Lua script table, as `entity.bLocked` at
     * runtime and `Properties.Lock.bLocked` at design time. Every accessor goes
     * through `GetEntityScriptTable`, which is null for an entity with no
     * script proxy -- so there `IsLocked` reads false, `SetLocked` does nothing
     * and `CanUnlockWithDynamicKey` reads true.
     *
     * `C_LockpickableEntity` allocates exactly 112 bytes and adds no members,
     * which is what proves this size.
     */
    class C_LockBase: public SDK::IGameObjectExtension {
      public:
        ~C_LockBase() override = default;

        /**
         * The name `IGameObject::QueryExtension` finds this extension by.
         *
         * `C_LockBase::Init` calls it on itself and inserts the result into the
         * process-global set of lock-base extension names.
         */
        virtual const char *GetExtensionName() const = 0; // slot 29

        /**
         * Whether `keyItemClass` opens this lock.
         *
         * The base does not read `this` at all: it asks the soul system for the
         * player's soul and answers whether that inventory holds an item of the
         * class. `C_Stash` also allows the dynamic-key path. Which GUID is
         * tested is chosen by the entity's Lua. Answers only for the local
         * player -- there is no per-player dimension in it.
         */
        virtual bool CanUnlock(const CryGUID &keyItemClass) const = 0; // slot 30

        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;

        /** Fired from Lua through `C_ScriptBindLockBase::ReportLocked`. */
        SignalLayout *GetOnLockedSignal() {
            return &m_onLocked;
        }

        /** Fired from Lua through `C_ScriptBindLockBase::ReportUnlocked`. */
        SignalLayout *GetOnUnlockedSignal() {
            return &m_onUnlocked;
        }

        /** Fired from Lua through `C_ScriptBindLockBase::ReportLockpicked`. */
        SignalLayout *GetOnLockpickedSignal() {
            return &m_onLockpicked;
        }

        /** The runtime `entity.bLocked`, the flag the game itself tests. */
        bool IsLocked() const {
            return hook::this_call<bool>(gPatterns.C_LockBase__IsLocked, this);
        }

        /** The design-time `Properties.Lock.bLocked` the level author set. */
        bool GetPropertiesLocked() const {
            return hook::this_call<bool>(gPatterns.C_LockBase__GetPropertiesLocked, this);
        }

        /**
         * Drives the lock through the entity's own Lua `Lock` / `Unlock`, and
         * is a no-op when the runtime flag already matches.
         *
         * `SetLocked(bool, std::function<void(IScriptSystem &)>)`: after the
         * method and `self` are pushed, the function pushes whatever else the
         * script's method takes -- AnimDoor's `Lock(dontClose)` is the one the
         * engine feeds (0x1808C7F58). The function is taken by value, so the
         * native receives its address and destroys it; the empty one pushes
         * nothing, as `ResetLockStatesToProperties` passes it.
         */
        void SetLocked(bool locked) {
            ScriptArgumentsFunction none;
            SetLocked(locked, none);
        }

        void SetLocked(bool locked, ScriptArgumentsFunction &arguments) {
            hook::this_call<void>(gPatterns.C_LockBase__SetLocked, this, locked, &arguments);
        }

        /**
         * The entity's Lua `OnLockpicked`, run as a successful pick runs it
         * (`C_LockBase::CallOnLockpickedScript`, 0x1819B8344), after
         * `C_LockPicking::CompleteUnlockedAttempt` has unlocked the lock and
         * broadcast `GetOnLockpickedSignal`.
         */
        void CallOnLockpickedScript() {
            hook::this_call<void>(gPatterns.C_LockBase__OnLockpicked, this);
        }

        /** `Properties.Lock.bCanUnlockWithDynamicKey`, defaulting to true. */
        bool CanUnlockWithDynamicKey() const {
            return hook::this_call<bool>(gPatterns.C_LockBase__CanUnlockWithDynamicKey, this);
        }

        /**
         * The base `CanUnlock`, called directly rather than through the
         * vtable: whether the local player holds an item of `keyItemClass`,
         * with none of the dynamic-key paths an override adds.
         */
        bool CanUnlockWithKey(const CryGUID &keyItemClass) const {
            return hook::this_call<bool>(gPatterns.C_LockBase__CanUnlock, this, &keyItemClass);
        }

      private:
        SignalLayout m_onLocked;     // 0x40
        SignalLayout m_onUnlocked;   // 0x50
        SignalLayout m_onLockpicked; // 0x60

        static void AssertLayout() {
            static_assert(offsetof(C_LockBase, m_onLocked) == 0x40, "C_LockBase::m_onLocked offset mismatch");
            static_assert(offsetof(C_LockBase, m_onUnlocked) == 0x50, "C_LockBase::m_onUnlocked offset mismatch");
            static_assert(offsetof(C_LockBase, m_onLockpicked) == 0x60, "C_LockBase::m_onLockpicked offset mismatch");
        }
    };

    static_assert(sizeof(C_LockBase) == 0x70, "C_LockBase size mismatch");

    /** A lockable object with no inventory; adds nothing but its own vtable. */
    class C_LockpickableEntity: public C_LockBase {
      public:
        ~C_LockpickableEntity() override = default;
    };

    static_assert(sizeof(C_LockpickableEntity) == 0x70, "C_LockpickableEntity size mismatch");

    /**
     * The `C_Stash` extension on `entityId`, or null. Queries all five stash
     * extension names -- Stash, StashCorpse, Nest, CartStash, DestroStash -- so
     * it answers for every variant.
     */
    inline C_Stash *FindStashExtension(EntityId entityId) {
        return hook::call<C_Stash *>(gPatterns.entitymodule__FindStashExtension, entityId);
    }

    // The engine's generic `FindLockBaseExtension(IGameObject *)` at
    // 0x180CC5958 is deliberately not exposed. It is byte-identical to an
    // unrelated helper at 0x180DBFA84 apart from the RIP-relative global it
    // walks, so no entry-point signature can tell the two apart, and resolving
    // it by position would break on any function reordering. Nothing here
    // needs it: a stash's lock base is the stash, while door synchronization
    // resolves the shipped AnimDoor entity by its stable IEntity GUID and
    // drives that entity's own Lua table.
} // namespace SDK::wh::entitymodule
