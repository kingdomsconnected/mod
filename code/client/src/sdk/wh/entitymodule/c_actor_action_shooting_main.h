#pragma once

#include "c_actor_action_item_handling_anim.h"
#include "c_actor_action_shooting_charging.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The actor action the whole missile system runs inside.
     *
     * It holds one sub-action at a time -- aiming, charging, instant
     * charging, firing, reloading,
     * uncharging, unloading, transitioning to idle -- and that sub-action is
     * what `C_ActorShootingExpansion::IsCharging`, `IsReloading` and
     * `IsFiring` interrogate. `C_ActorActionShootingBase::Enter` writes the
     * pointer below and its `Exit` clears it, which is why those two are the
     * only edges a missile phase can change on.
     *
     * The factory allocates 0xB8 bytes; the constructor writes vtable
     * pointers at 0x00, 0x10 and 0x50 and nulls the current action at 0x98.
     * Constructor 0x182893D08 calls the non-animation actor-action base
     * constructor 0x1809CDA38; its C_ActorObject provides the owner at 0x58.
     */
    class C_ActorActionShootingMain: public C_ActorAction<I_ActorAction, framework::C_Action<I_ActorAction>> {
      public:
        /**
         * Whether a shooting sub-action is current.
         *
         * The three expansion predicates dereference this pointer without
         * checking it, so anything that can run between one sub-action's
         * `Exit` and the next one's `Enter` has to ask here first. The
         * engine's own guard does not cover that window: it is the exiting
         * flag the framework's exit dispatcher sets on the action *being*
         * exited, which is the sub-action and never this one.
         */
        bool HasCurrentAction() const {
            return m_pCurrentAction != nullptr;
        }

        /**
         * Whether the actor is aiming: the latch `NotifyAimBegin`
         * (0x18289FC84) sets from `C_ActorActionShootingAiming::Enter` once a
         * draw completes, and `NotifyAimEnd` (0x18289690C) clears on
         * uncharging, on the end of a shot and on this action's `Exit`. The
         * same edges raise the RPG combat aim-begin and aim-end events.
         */
        bool IsAiming() const {
            return m_aiming;
        }

        /** The sub-action the constructor (0x182893D08) was told to begin in. */
        std::uint32_t GetInitialActionType() const {
            return m_initialActionType;
        }

        /**
         * Whether the drawn arrow or bolt is attached to the hand as a helper
         * item; `RemoveShootingHelperItem` (0x1828964B8) detaches it and
         * clears this.
         */
        bool HasHelperItem() const {
            return m_helperItemAttached;
        }

      private:
        // Animation tag ids InitializeAnimationTagIds18289C608 resolves by name in the constructor.
        std::uint32_t m_arrowReleaseTagId;    // 0x68, "arrow_release"
        std::uint32_t m_bowShotTagId;         // 0x6C, "c_w_bow_shot"
        std::uint32_t m_crossbowTypeTagId;    // 0x70, "crossbow_type"
        std::uint32_t m_thunderousBlastTagId; // 0x74, "thunderous_blast"
        // A std::optional<C_ItemWrapper>: Enter (0x1828977F4) resets it when engaged, then installs the
        // C_ItemWrapper vftable at 0x78, nulls the item at 0x80, clears 0x88 and engages it; dtor 0x182895300
        // destroys it through slot 0 when engaged; RemoveShootingHelperItem passes it to C_ItemHolder::RemoveItem.
        std::byte m_helperItem0078[0x18];     // 0x78, C_ItemWrapper storage
        bool m_helperItemEngaged;             // 0x90, ctor zeroes
        std::byte m_padding0091[0x7];

        // A C_ActorActionShootingBase, left untyped: nothing here calls
        // through it, and which of the eight sub-states it is would be read
        // off its vtable rather than off this pointer's declared type.
        void *m_pCurrentAction;            // 0x98
        std::uint32_t m_initialActionType; // 0xA0
        // Read only by CanStartReloadOrCharge (0x182895DE4), which requires it
        // set. Its one writer is a sub-action's fragment finish (0x18289D050),
        // which sets it only around its broadcast of the expansion's +0x18
        // signal: the one window in which a running action takes a queued
        // reload or draw. The Firing sub-action's +0xA4 is a field of that
        // class, not this one.
        std::byte m_opaque00A4[0x1];
        bool m_aiming;             // 0xA5
        bool m_helperItemAttached; // 0xA6
        std::byte m_padding00A7[0x1];
        std::uint32_t m_values00A8[3];        // 0xA8, ctor zeroes
        std::byte m_padding00B4[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionShootingMain, m_pCurrentAction) == 0x98, "C_ActorActionShootingMain::m_pCurrentAction offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_initialActionType) == 0xA0, "C_ActorActionShootingMain::m_initialActionType offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_aiming) == 0xA5, "C_ActorActionShootingMain::m_aiming offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_helperItemAttached) == 0xA6, "C_ActorActionShootingMain::m_helperItemAttached offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_arrowReleaseTagId) == 0x68, "C_ActorActionShootingMain::m_arrowReleaseTagId offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_thunderousBlastTagId) == 0x74, "C_ActorActionShootingMain::m_thunderousBlastTagId offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_helperItemEngaged) == 0x90, "C_ActorActionShootingMain::m_helperItemEngaged offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_helperItem0078) == 0x78, "C_ActorActionShootingMain::m_helperItem0078 offset mismatch");
            static_assert(offsetof(C_ActorActionShootingMain, m_values00A8) == 0xA8, "C_ActorActionShootingMain::m_values00A8 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionShootingMain) == 0xB8, "C_ActorActionShootingMain size mismatch");
} // namespace SDK::wh::entitymodule
