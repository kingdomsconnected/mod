#pragma once

#include "../../patterns.h"
#include "../shared/c_signal.h"
#include "c_actor_action_shooting_main.h"
#include "i_action_actor_expansion.h"

#include <utils/hooking/hooking.h>

#include <cstddef>

namespace SDK::wh::entitymodule {
    class C_ActionActor;
    class C_Item;

    class C_ActorShootingExpansion: public I_ActionActorExpansion {
      public:
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        C_Item *GetWeapon() const {
            return m_pWeapon;
        }

        C_Item *GetAmmunition() const {
            return m_pAmmunition;
        }

        /**
         * Whether a missile weapon with usable ammunition is in the hands.
         * `TryActivate` (0x180940DD0) raises it from `OnHandledItemChanged`
         * (0x180952108), the combat model's held-item signal, and from
         * `SetAmmunition`; `Deactivate` (0x1804CBDA0) lowers it when the
         * weapon leaves the hands or the ammunition goes.
         */
        bool IsActive() const {
            return m_active;
        }

        /**
         * Whether the weapon has fired since it was last activated: the
         * "fired" animation tag. `MarkWeaponFired` (0x182ACDBE0) raises it,
         * `Deactivate` (0x1804CBDA0) lowers it, `RefreshAnimationTags` pushes it.
         */
        bool HasFired() const {
            return m_fired;
        }

        /**
         * Whether the draw is past its release point -- the charged animation
         * tag. C_ActorActionShootingCharging::Update (0x1828A1044) writes it
         * every frame from that sub-action's CanRelease, and
         * `CompleteInstantCharging` raises it; `LaunchProjectile`, uncharging's
         * `Enter` and the main action's `Exit` lower it.
         */
        bool IsCharged() const {
            return m_charged;
        }

        /**
         * A reload owed to ammunition being equipped (equipment slot type 27,
         * `OnEquipmentItemChanged` 0x1819BEC18), which `ProcessPendingReload`
         * (0x180737A2C) carries out on the next update.
         */
        bool HasAmmunitionReloadPending() const {
            return m_ammunitionReloadPending;
        }

        /** A `Reload` whose action could not be queued, retried by `ProcessPendingReload`. */
        bool HasReloadPending() const {
            return m_reloadPending;
        }

        /**
         * The draw, 0 to 1, cached by `UpdatePlayerChargeState` (0x1804A8AF4)
         * for a player-controlled actor only: on any other body it stays 0.
         */
        float GetPlayerCharge() const {
            return m_playerCharge;
        }

        void RefreshAnimationTags() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__RefreshAnimationTags, this);
        }

        void MarkWeaponFired() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__MarkWeaponFired, this);
        }

        void SetAmmunition(C_Item *item) {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__SetAmmunition, this, item);
        }

        void Reload(bool force, bool instant) {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__Reload, this, force, instant);
        }

        void Charge() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__Charge, this);
        }

        void CancelCharging() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__CancelCharging, this);
        }

        void CancelReloading() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__CancelReloading, this);
        }

        void Fire() {
            hook::this_call<void>(gPatterns.C_ActorShootingExpansion__Fire, this, std::int32_t {0});
        }

        /**
         * Whether a shooting sub-action is current.
         *
         * The three predicates below dereference the main action's current
         * sub-action without checking it, so ask this first from anywhere
         * that can run on an engine edge -- `C_ActorActionShootingBase::Exit`
         * clears that pointer while the main action is still live, and the
         * engine's own guard is the exiting flag of the sub-action rather
         * than of the main. With no sub-action current the phase is idle.
         */
        bool HasShootingAction() const {
            return m_pMainAction != nullptr && m_pMainAction->HasCurrentAction();
        }

        /** Whether the actor is aiming its drawn weapon; see C_ActorActionShootingMain::IsAiming. */
        bool IsAiming() const {
            return m_pMainAction != nullptr && m_pMainAction->IsAiming();
        }

        bool IsCharging() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__IsCharging, this);
        }

        bool IsReloading() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__IsReloading, this);
        }

        bool IsFiring() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__IsFiring, this, std::uint64_t {0});
        }

        bool CanFire() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__CanFire, this, false);
        }

        bool CanReload() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__CanReload, this);
        }

        bool CanCharge() const {
            return hook::this_call<bool>(gPatterns.C_ActorShootingExpansion__CanCharge, this);
        }

      private:
        // Three model signals the constructor (0x1808DDB78) builds.
        wh::shared::C_Signal m_signal0008; // 0x08
        wh::shared::C_Signal m_signal0018; // 0x18
        wh::shared::C_Signal m_signal0028; // 0x28
        C_ActionActor *m_pActionActor;            // 0x38
        C_Item *m_pWeapon;                        // 0x40
        C_Item *m_pAmmunition;                    // 0x48
        C_ActorActionShootingMain *m_pMainAction; // 0x50
        // Set while TryActivate's equipment and combat listeners are connected.
        bool m_listening;               // 0x58
        bool m_active;                  // 0x59
        bool m_fired;                   // 0x5A
        bool m_charged;                 // 0x5B
        bool m_ammunitionReloadPending; // 0x5C
        bool m_reloadPending;           // 0x5D
        bool m_reloadPendingForce;      // 0x5E, the force argument the retry passes
        std::byte m_opaque005F[0x1];
        float m_playerCharge; // 0x60
        std::byte m_opaque0064[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_ActorShootingExpansion, m_listening) == 0x58, "C_ActorShootingExpansion::m_listening offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_fired) == 0x5A, "C_ActorShootingExpansion::m_fired offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_charged) == 0x5B, "C_ActorShootingExpansion::m_charged offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_reloadPendingForce) == 0x5E, "C_ActorShootingExpansion::m_reloadPendingForce offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_playerCharge) == 0x60, "C_ActorShootingExpansion::m_playerCharge offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_pActionActor) == 0x38, "C_ActorShootingExpansion::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_pWeapon) == 0x40, "C_ActorShootingExpansion::m_pWeapon offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_pAmmunition) == 0x48, "C_ActorShootingExpansion::m_pAmmunition offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_pMainAction) == 0x50, "C_ActorShootingExpansion::m_pMainAction offset mismatch");
            static_assert(offsetof(C_ActorShootingExpansion, m_active) == 0x59, "C_ActorShootingExpansion::m_active offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorShootingExpansion) == 0x68, "C_ActorShootingExpansion size mismatch");
} // namespace SDK::wh::entitymodule
