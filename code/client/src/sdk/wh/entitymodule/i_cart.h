#pragma once

#include "../../cryengine/action/i_actor.h"
#include "../../types.h"
#include "e_cart_horse_slot.h"
#include "e_cart_move_blocker.h"
#include "e_cart_position.h"
#include "e_cart_state.h"

#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * Cart and wagon contract, implemented only by C_Cart.
     *
     * RTTI preserves the hierarchy through
     * CGameObjectExtensionHelper<C_Cart, I_Cart, 64>, so I_Cart is an
     * IGameObjectExtension like IActor is. C_Cart's complete table at
     * 0x183A3C5B0 holds 54 slots: IGameObjectExtension's 29, then I_Cart's own
     * 25 starting at slot 29 / +0x0E8 -- the same boundary IActor::SetHealth
     * sits on.
     *
     * There is no cart factory behind this interface. "WHCart" is registered
     * once, by IGameObjectSystem::RegisterExtension at 0x1811AA85C, together
     * with an entity class of the same name and
     * Scripts/Entities/WH/Vehicles/WHCart.lua; nothing in the module ever looks
     * that class up again at runtime. A cart therefore comes into existence as
     * an ordinary IEntitySystem::SpawnEntity of class "WHCart" and leaves
     * through IEntitySystem::RemoveEntity.
     *
     * Six slots are named for their index rather than their purpose. Each is
     * understood mechanically -- the comment says what it touches -- but not
     * well enough to be given a name that would read as a claim.
     */
    class I_Cart: public SDK::IGameObjectExtension {
      public:
        virtual ~I_Cart() = default;

        /** Idle when the cart has no move context at all. */
        virtual E_CartState::Type GetState() const = 0; // +0x0E8

        /** Returns a transform read out of the follower model at C_Cart+0x98, identity when there is none. */
        virtual QuatT VirtualSlot30() const = 0; // +0x0F0

        /**
         * Puts the cart at the point of its path nearest `position`, facing
         * along `direction` when that is not zero, and with `poseNow` poses
         * every part there straight away rather than on the next tick. False
         * without a follower model or when the point is more than 3 m off the
         * path. What the behaviour tree's `CartMove` does at `Speed=instant`.
         */
        virtual bool Teleport(const Vec3 &position, const Vec3 &direction, bool poseNow) = 0; // +0x0F8

        /**
         * Sets the move target to the point of the path nearest `position`
         * (`BeginMove`, 0x1828DB1E8). An idle cart goes to `NeedConfirmation`
         * or `WaitingForCaravan`, and `ConfirmMoveStart` lets it go; a cart in
         * any other state goes straight to `Moving`. A target behind the cart
         * backs it up. False when the point is off the path. Dereferences the
         * follower model unchecked, so never call it on a cart the setup pass
         * has not built.
         */
        virtual bool MoveTo(const Vec3 &position) = 0; // +0x100

        /**
         * Immediate drops the move context straight to Idle; otherwise a
         * moving cart is put into Stopping and coasts down.
         */
        virtual void Stop(bool immediate) = 0; // +0x108

        /** E_CartSpeed::Fast or Normal, the byte at +0x09 of the move context. */
        virtual void SetFastSpeed(bool fast) = 0; // +0x110

        /** m_moveBlockers[blocker] = value. Blockers are independent; setting one leaves the rest alone. */
        virtual void SetMoveBlocker(E_CartMoveBlocker::Type blocker, std::int32_t value) = 0; // +0x118

        /**
         * Where a horse in `slot` stands, in world space: the chassis's
         * `slt_horse_left` or `slt_horse_right` helper composed with its pose.
         * False before the setup pass has read the helpers.
         */
        virtual bool GetHarnessTransform(E_CartHorseSlot::Type slot, QuatT &transform) const = 0; // +0x120

        /** Where an actor sits, or walks, for this seat. Teamster takes a separate path from the rest. */
        virtual bool GetPositionTransform(E_CartPosition::Type position, QuatT &transform) const = 0; // +0x128

        /** Entity id of the horse hitched into the slot, 0 when empty. */
        virtual EntityId GetHorseAtSlot(E_CartHorseSlot::Type slot) const = 0; // +0x130

        /** Entity id of the actor holding the seat, 0 when empty. */
        virtual EntityId GetActorAtPosition(E_CartPosition::Type position) const = 0; // +0x138

        /**
         * Hitches a horse. Re-hitching the same actor only bumps the slot's
         * reference count; a fresh hitch is gated on CanOccupyHorseSlot and
         * notifies the cart's horse-slot listeners.
         */
        virtual bool OccupyHorseSlot(C_Actor *actor, E_CartHorseSlot::Type slot) = 0; // +0x140

        /**
         * Takes a seat. Same reference-counting rule as OccupyHorseSlot, gated
         * on CanOccupyPosition. Note the argument order is the mirror of
         * CanOccupyPosition's -- that is what the game does.
         */
        virtual bool OccupyPosition(C_Actor *actor, E_CartPosition::Type position) = 0; // +0x148

        /** Drops one reference; the slot only empties when the last one goes. */
        virtual void LeaveHorseSlot(E_CartHorseSlot::Type slot) = 0; // +0x150
        virtual void LeavePosition(E_CartPosition::Type position) = 0; // +0x158

        /**
         * The seat must be script-enabled and unoccupied. Codriver and
         * RightBack additionally require a player-controlled actor; every
         * other seat requires the opposite.
         */
        virtual bool CanOccupyPosition(E_CartPosition::Type position, C_Actor *actor) const = 0; // +0x160

        /** The slot must be empty, and the follower model must have a harness helper for it (0x1828D4BB4). */
        virtual bool CanOccupyHorseSlot(E_CartHorseSlot::Type slot) const = 0; // +0x168

        /** The script-enabled flag for Teamster specifically. */
        virtual bool IsTeamsterPositionEnabled() const = 0; // +0x170

        /** GetState() == Idle. */
        virtual bool IsIdle() const = 0; // +0x178

        /**
         * Acquires or releases the counter at C_Cart+0x15C and returns its new
         * value (0x1828D74F8). `C_CartPlayerControl::CanPlayerMount`
         * (0x1828F1B50) refuses every player seat while it is zero, unless
         * the `wh_cart_ForceEnablePlayerSeat` CVar is positive; its soul-side
         * check runs either way.
         */
        virtual std::uint32_t AddPlayerSeatEnable(bool acquire) = 0; // +0x180

        /**
         * COMDAT-folded generic accessors shared with unrelated classes, so
         * neither carries a cart-specific symbol. They return the object
         * C_Cart::Init allocates at +0xA8, and the address of the embedded
         * block at +0x138.
         */
        virtual void *VirtualSlot49() = 0; // +0x188
        virtual void *VirtualSlot50() = 0; // +0x190

        /**
         * The argument is a pointer to a 16-byte delegate, copied into the
         * listener set at C_Cart+0x40; removal matches on both halves. The
         * listener interface itself has not been modeled.
         */
        virtual void AddListener(void *listener) = 0; // +0x198
        virtual void RemoveListener(void *listener) = 0; // +0x1A0

        /**
         * Tests the per-position reservation array at C_Cart+0xCC for exactly
         * 1 (0x1828DA800), which is distinct from seat occupancy: C_CartPlayerControl::CanPlayerMount
         * refuses the back seat when RightFront, LeftBack or RightBack is
         * reserved, whether or not anyone is sitting there.
         */
        virtual bool IsPositionReserved(E_CartPosition::Type position) const = 0; // +0x1A8
    };
} // namespace SDK::wh::entitymodule
