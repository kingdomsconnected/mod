#pragma once

#include "../../patterns.h"
#include "../shared/c_recursive_srw_lock.h"
#include "c_cart_follower_model.h"
#include "c_cart_move_context.h"
#include "e_cart_horse_slot.h"
#include "e_cart_move_blocker.h"
#include "e_cart_position.h"
#include "i_cart.h"
#include "i_cart_driver.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One of C_Cart's three listener collections.
     *
     * Not the shared wh::shared::C_Listeners: this one has no vtable, and the
     * constructor initializes it as a payload pointer followed by two int16
     * re-entrancy cursors, both -1 while idle.
     */
    struct S_CartListenerSet {
        void *pEntries;                  // 0x00
        std::int16_t dispatchCursors[2]; // 0x08
        std::byte padding000C[0x4];      // 0x0C
    };

    static_assert(sizeof(S_CartListenerSet) == 0x10, "S_CartListenerSet size mismatch");

    /**
     * A cart seat or harness slot. Occupancy is reference-counted rather than
     * boolean: taking a slot the same actor already holds only bumps refCount,
     * and the slot empties when the last reference is dropped.
     */
    struct S_CartOccupancy {
        EntityId occupant;      // 0x00
        std::uint32_t refCount; // 0x04
    };

    static_assert(sizeof(S_CartOccupancy) == 0x8, "S_CartOccupancy size mismatch");

    /**
     * Cart and wagon game-object extension, attached to every entity of class
     * "WHCart".
     *
     * RTTI proves CGameObjectExtensionHelper<C_Cart, I_Cart, 64>, and the
     * creator's allocation of 368 bytes proves the complete 0x170-byte size.
     * The vtable at 0x183A3C5B0 runs to 54 slots; the constructor at
     * 0x1804CF408 writes or fills every member named below, which is what
     * fixes their offsets.
     *
     * A cart is an entity hierarchy, not a single entity. The chassis carries
     * this extension; the trailing half of a wagon and the horses are child
     * entities on the "slt_back_part", "slt_horse_left" and "slt_horse_right"
     * attachment slots, driven from the pivots named by E_CartFollowerPart.
     * That is why the lookup helpers walk to the hierarchy root before asking
     * for the extension.
     *
     * Which seats exist is data, not code: the per-position enabled flags come
     * from the cart's smart object, looked up by helper name
     * (`IsCartPositionEnabledBySmartObject`, 0x180F8F81C).
     *
     * Movement state does not live here. C_Cart holds a follower model and a
     * move context, and the E_CartState byte is at +0x08 of that move context
     * -- which is why I_Cart::GetState reports Idle whenever the context is
     * missing. The path is a third object again, held by the follower model.
     */
    // Not `final`: I_Cart's slots are pure here and this class overrides none
    // of them, so a final class would have every call through a C_Cart*
    // devirtualized into a link against a body that does not exist.
    class C_Cart: public I_Cart {
      public:
        ~C_Cart() override = default;

        /** The seat's script-enabled flag. IsTeamsterPositionEnabled is this one for Teamster. */
        bool IsPositionEnabled(E_CartPosition::Type position) const {
            return m_positionEnabled[position];
        }

        /** Nothing consumes a blocker as a bool; the game stores and compares the whole int. */
        std::int32_t GetMoveBlocker(E_CartMoveBlocker::Type blocker) const {
            return m_moveBlockers[blocker];
        }

        /** The seat's enabled flag, which only the setup pass writes -- from the cart's smart object. */
        void SetPositionEnabled(E_CartPosition::Type position, bool enabled) {
            m_positionEnabled[position] = enabled;
        }

        /** How the parts follow the path, or nullptr until the setup pass has found the chassis. */
        C_CartFollowerModel *GetFollowerModel() const {
            return m_pFollowerModel;
        }

        /** Where the cart is along its path, or nullptr until the setup pass. */
        C_CartMoveContext *GetMoveContext() const {
            return m_pMoveContext;
        }

        /**
         * The lock every native that moves the cart holds: the per-frame tick,
         * `ConfirmMoveStart`, `SetDriver`. Anything that changes the path or
         * the target from outside the engine's own tick takes it too, because
         * the tick is not guaranteed to run on the thread that calls this.
         */
        shared::C_RecursiveSRWLock *GetLock() {
            return &m_lock;
        }

        /**
         * The setup pass, as entity event 36 runs it: finds the `GeomEntity`
         * chassis the `WHCart` hangs off, builds the follower model and the
         * move context from it, and fills the seats from the smart object.
         *
         * It returns early -- before its already-done latch at +0x88 -- while
         * the `WHCart` has no parent, which is always the case at
         * `InitEntity` for a cart assembled at runtime. So it is asked for
         * once the tree is up.
         */
        void RunSetupPass() {
            hook::this_call<void>(gPatterns.C_Cart__SetupCartOnReset, this, true);
        }

        /** NeedConfirmation to Moving: what the driver's reins flick lets happen. No-op in any other state. */
        void ConfirmMoveStart() {
            hook::this_call<void>(gPatterns.C_Cart__ConfirmMoveStart, this);
        }

        /** Whatever holds the reins, or nullptr: without it the cart does not move at all. */
        I_CartDriver *GetDriver() const {
            return m_pDriver;
        }

        /**
         * Hands the reins to `driver`, or takes them back with nullptr
         * (0x1828DC678): the movement's speeds are refilled from the driver's
         * body, and the `noDriver` move blocker follows. Takes the cart's own
         * lock.
         */
        void SetDriver(I_CartDriver *driver) {
            hook::this_call<void>(gPatterns.C_Cart__SetDriver, this, driver);
        }

        /** The cart extension on `cartEntityId`, the `WHCart` entity; nullptr for anything else. */
        static C_Cart *FindByEntityId(EntityId cartEntityId) {
            return hook::call<C_Cart *>(gPatterns.FindCartByEntityId, cartEntityId);
        }

      private:
        // AddListener and RemoveListener drive the first set. The third is the
        // one Occupy/LeaveHorseSlot notify. Nothing has been proven about the
        // second beyond its construction.
        S_CartListenerSet m_listeners;          // 0x40
        S_CartListenerSet m_listeners0050;      // 0x50
        S_CartListenerSet m_horseSlotListeners; // 0x60

        /** The frame the movement last ticked on; `TickCartOncePerFrame` runs once per value. */
        std::uint32_t m_tickFrame; // 0x70
        std::byte m_padding0074[0x4];

        shared::C_RecursiveSRWLock m_lock; // 0x78

        /** The setup pass's latch; set on its first complete run. */
        bool m_setupDone; // 0x88
        bool m_state0089;

        /** Set by entity events 4 and 0x3E and cleared by 0x3D; the movement tick is skipped while set (UpdateFromHitchedHorse 0x180F8DACC). */
        bool m_updateSuspended; // 0x8A
        std::byte m_padding008B[0x5];

        /** The I_CartDriver at the reins, written by `SetDriver` (0x1828DC678). */
        I_CartDriver *m_pDriver; // 0x90

        C_CartFollowerModel *m_pFollowerModel; // 0x98
        C_CartMoveContext *m_pMoveContext;     // 0xA0

        // Allocated by C_Cart::Init at 80 and 72 bytes respectively and owned
        // for the life of the extension. Neither type has been modeled.
        void *m_pOwned00A8; // 0xA8
        void *m_pOwned00B0; // 0xB0

        std::int32_t m_moveBlockers[E_CartMoveBlocker::Count]; // 0xB8

        /**
         * Reservation is separate from occupancy: a seat can be reserved with
         * nobody in it, and CanPlayerMount refuses the back of the cart on the
         * strength of this array alone.
         */
        std::int32_t m_positionReserved[E_CartPosition::Count]; // 0xCC

        S_CartOccupancy m_positions[E_CartPosition::Count];   // 0xE8
        S_CartOccupancy m_horseSlots[E_CartHorseSlot::Count]; // 0x120

        bool m_positionEnabled[E_CartPosition::Count]; // 0x130
        std::byte m_padding0137[0x1];

        /** Embedded block whose address the folded I_Cart::VirtualSlot50 returns. */
        std::byte m_context0138[0x18]; // 0x138

        bool m_state0150; // 0x150
        std::byte m_padding0151[0xB];

        /** Acquired and released through I_Cart::AddPlayerSeatEnable. */
        std::uint32_t m_slot48Counter; // 0x15C
        // Ctor 0x1804CF408 zeroes 0x15C..0x16C as two unaligned qword stores.
        std::uint32_t m_value0160;      // 0x160
        std::uint32_t m_values0164[2];  // 0x164
        std::byte m_padding016C[0x4];

        // Never called. The offsets are what make the members above usable, so
        // they are pinned here, where the type is complete and its own members
        // are in scope.
        static void AssertLayout() {
            static_assert(offsetof(C_Cart, m_listeners) == 0x40, "C_Cart::m_listeners offset mismatch");
            static_assert(offsetof(C_Cart, m_listeners0050) == 0x50, "C_Cart::m_listeners0050 offset mismatch");
            static_assert(offsetof(C_Cart, m_horseSlotListeners) == 0x60, "C_Cart::m_horseSlotListeners offset mismatch");
            static_assert(offsetof(C_Cart, m_tickFrame) == 0x70, "C_Cart::m_tickFrame offset mismatch");
            static_assert(offsetof(C_Cart, m_lock) == 0x78, "C_Cart::m_lock offset mismatch");
            static_assert(offsetof(C_Cart, m_setupDone) == 0x88, "C_Cart::m_setupDone offset mismatch");
            static_assert(offsetof(C_Cart, m_updateSuspended) == 0x8A, "C_Cart::m_updateSuspended offset mismatch");
            static_assert(offsetof(C_Cart, m_pDriver) == 0x90, "C_Cart::m_pDriver offset mismatch");
            static_assert(offsetof(C_Cart, m_pFollowerModel) == 0x98, "C_Cart::m_pFollowerModel offset mismatch");
            static_assert(offsetof(C_Cart, m_pMoveContext) == 0xA0, "C_Cart::m_pMoveContext offset mismatch");
            static_assert(offsetof(C_Cart, m_pOwned00A8) == 0xA8, "C_Cart::m_pOwned00A8 offset mismatch");
            static_assert(offsetof(C_Cart, m_pOwned00B0) == 0xB0, "C_Cart::m_pOwned00B0 offset mismatch");
            static_assert(offsetof(C_Cart, m_moveBlockers) == 0xB8, "C_Cart::m_moveBlockers offset mismatch");
            static_assert(offsetof(C_Cart, m_positionReserved) == 0xCC, "C_Cart::m_positionReserved offset mismatch");
            static_assert(offsetof(C_Cart, m_positions) == 0xE8, "C_Cart::m_positions offset mismatch");
            static_assert(offsetof(C_Cart, m_horseSlots) == 0x120, "C_Cart::m_horseSlots offset mismatch");
            static_assert(offsetof(C_Cart, m_positionEnabled) == 0x130, "C_Cart::m_positionEnabled offset mismatch");
            static_assert(offsetof(C_Cart, m_context0138) == 0x138, "C_Cart::m_context0138 offset mismatch");
            static_assert(offsetof(C_Cart, m_state0150) == 0x150, "C_Cart::m_state0150 offset mismatch");
            static_assert(offsetof(C_Cart, m_slot48Counter) == 0x15C, "C_Cart::m_slot48Counter offset mismatch");
            static_assert(offsetof(C_Cart, m_value0160) == 0x160, "C_Cart::m_value0160 offset mismatch");
            static_assert(offsetof(C_Cart, m_values0164) == 0x164, "C_Cart::m_values0164 offset mismatch");
        }
    };

    static_assert(sizeof(C_Cart) == 0x170, "C_Cart size mismatch");
} // namespace SDK::wh::entitymodule
