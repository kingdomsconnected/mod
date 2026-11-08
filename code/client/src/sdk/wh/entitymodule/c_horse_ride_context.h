#pragma once

#include "../../patterns.h"
#include "s_actor_action_dismount_params.h"
#include "s_actor_action_mount_params.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ActorActionRider;

    /**
     * A ride's state machine, built by the rider action's constructor.
     *
     * Twenty-four bytes with no vtable, holding a back-pointer to the action
     * that owns it. Every edge passes through `SetState`: the mount from that
     * action's entry, the get-off from a dismount request, and the tear-down
     * from its exit, which drives the state to none through a forced dismount
     * rather than clearing it.
     *
     * It moves when the engine *decides*, rather than when the animation it
     * queues finishes, which is what makes it the right place to read a
     * transition from. Who is actually in the saddle is a different question,
     * and the horse's own rider-sync answers that one.
     */
    class C_HorseRideContext {
      public:
        /**
         * Eleven states, not four, harvested from every literal reaching
         * `SetState`. `QueueTransitionAction` hands anything outside
         * {0,1,2,3,11} to `C_ActorActionRiderAnim::Create` and lets Mannequin
         * pick the fragment -- the middle of the enum is the animal telling
         * the rider what it is doing.
         */
        enum RideState : std::uint8_t {
            RIDE_STATE_NONE        = 0,  // tear-down's end
            RIDE_STATE_MOUNTING    = 1,  // C_ActorActionRider::Enter
            RIDE_STATE_RIDING      = 2,  // the steady state, and every fallback
            RIDE_STATE_DISMOUNTING = 3,  // RequestDismount, tear-down
            RIDE_STATE_SYNC_4      = 4,  // rider-sync slot 2
            RIDE_STATE_SYNC_5      = 5,  // rider-sync slot 1
            RIDE_STATE_SYNC_6      = 6,  // rider-sync slot 6; the reins pulled hard
            RIDE_STATE_SYNC_7      = 7,  // rider-sync slot 4 sets 7..9
            RIDE_STATE_SYNC_8      = 8,
            RIDE_STATE_SYNC_9      = 9,
            RIDE_STATE_SYNC_10     = 10,
            RIDE_STATE_BONDING     = 11, // fragment HRAC_KUN_BONDING
        };

        /**
         * Whether a rider in this state is on the animal.
         *
         * A deny-list, and it has to be one: listing the states that count as
         * seated drops a rider out of his own saddle the moment the horse
         * does anything, since braking hard alone settles on 6.
         */
        static bool IsInSaddle(std::uint8_t state) {
            return state != RIDE_STATE_NONE;
        }

        /** The rider action that built this, and whose horse it is for. */
        C_ActorActionRider *GetAction() const {
            return m_pAction;
        }

        RideState GetState() const {
            return static_cast<RideState>(m_state);
        }

        /**
         * The flank the get-off plays on, as `StopRide` takes it: 1 or 2.
         * `RequestDismount` (0x180AD29B8) stores side == 1 at +0x09, and the
         * dismount builder picks the fragment's side tag from it.
         */
        std::uint8_t GetDismountSide() const {
            return m_dismountSide ? 1 : 2;
        }

        /** Copied unread into the dismount parameters at +0x20; meaning unverified. */
        std::uint8_t GetDismountFlag() const {
            return m_dismountFlag;
        }

        /**
         * Drives the ride to `state`, which is how every edge of a ride moves.
         *
         * 0x180619210. The mount comes through here from
         * `C_ActorActionRider::Enter`, the get-off from `RequestDismount` and
         * the tear-down from `TearDownRide`; nothing else writes the byte.
         *
         * It cancels the running transition action first, whatever the
         * destination (0x1806196FC). A mount cancelled that way exits through
         * `C_ActorActionMount::Exit` (0x180AD2EF8), which reports failure and
         * detaches the rider unless the mount's success callback already ran:
         * the context then says RIDING over a body that is not attached.
         *
         * **This is the only way to end a ride that cannot be refused.**
         * `RequestDismount` -- and therefore `StopRide`, `DismountHorse` and
         * Lua's `ForceDismount` -- runs `ChooseDismountSide` first *even when
         * force is set*, and that answers 0 when geometry blocks both flanks,
         * at which point the whole request returns having done nothing and
         * still reports success. Beside a horse that has fallen over, that is
         * the normal outcome, and the rider is left linked to the animal:
         * `C_RiderSync::DetachFromHorse` runs only from
         * `C_ActorActionRider::Exit`, and the action only exits once this byte
         * reaches `RIDE_STATE_NONE`.
         *
         * `force` skips the transition animation. Forced into
         * `RIDE_STATE_DISMOUNTING` it runs `ComputeDismountPlacement` inline,
         * which has no geometry check of its own -- it uses the side byte the
         * last `ChooseDismountSide` left -- moves the rider there, reports the
         * dismount and settles on none: a placed get-off that cannot be
         * refused. Forced into `RIDE_STATE_NONE` there is no placement at all.
         */
        void SetState(RideState state, bool force) {
            hook::this_call<void>(gPatterns.C_HorseRideContext__SetState, this, static_cast<std::uint8_t>(state), force);
        }

      private:
        C_ActorActionRider *m_pAction; // 0x00
        std::uint8_t m_state;          // 0x08
        bool m_dismountSide;            // 0x09, picks the dismount fragment's side tag (0x180A52378)
        std::uint8_t m_dismountFlag;    // 0x0A, copied into S_ActorActionDismountParams +0x20
        std::uint8_t m_placeOnDismount; // 0x0B, constructed as 1
        std::uint8_t m_value000C;       // 0x0C, zeroed
        std::byte m_opaque000D[0x3];    // 0x0D, not written
        std::uint64_t m_value0010;      // 0x10, zeroed

        static void AssertLayout() {
            static_assert(offsetof(C_HorseRideContext, m_pAction) == 0x00, "C_HorseRideContext::m_pAction offset mismatch");
            static_assert(offsetof(C_HorseRideContext, m_state) == 0x08, "C_HorseRideContext::m_state offset mismatch");
            static_assert(offsetof(C_HorseRideContext, m_dismountSide) == 0x09, "C_HorseRideContext::m_dismountSide offset mismatch");
            static_assert(offsetof(C_HorseRideContext, m_dismountFlag) == 0x0A, "C_HorseRideContext::m_dismountFlag offset mismatch");
            static_assert(offsetof(C_HorseRideContext, m_placeOnDismount) == 0x0B, "C_HorseRideContext::m_placeOnDismount offset mismatch");
            static_assert(offsetof(C_HorseRideContext, m_value0010) == 0x10, "C_HorseRideContext::m_value0010 offset mismatch");
        }
    };

    static_assert(sizeof(C_HorseRideContext) == 0x18, "C_HorseRideContext size mismatch");
} // namespace SDK::wh::entitymodule
