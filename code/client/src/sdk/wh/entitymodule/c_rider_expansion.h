#pragma once

#include "../../types.h"
#include "i_action_actor_expansion.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_ActionActor;

    /**
     * An actor's riding subsystem, expansion type 5 on its action queue.
     *
     * The engine's own way on and off a horse: `C_Actor` slots 203, 204 and
     * 205 -- what the Lua `CanMount`, `ForceMount` and `ForceDismount` reach
     * -- are three-line forwarders onto slots 8, 11 and 12 here.
     */
    class C_RiderExpansion: public I_ActionActorExpansion {
      public:

        /**
         * Whether this body may get onto `horse` at all. Slot 8, +0x40.
         *
         * Requires the horse's rider-sync property (`C_Horse+0x9E8`) to read
         * null, the target to be actor class 10, and one of the state flags
         * 0, 7, 19 or 31 on this body's own actor model -- which a puppet
         * never carries, so this refuses one. Reads both models at `+0x990`
         * unchecked, so ask it only about bodies the engine has finished
         * building.
         */
        virtual bool CanMount(C_Actor *horse) = 0;
        virtual void unk_fn0009()             = 0;
        virtual void unk_fn0010()             = 0;

        /**
         * Gets this body onto `horse`. Slot 11, +0x58.
         *
         * Asks `CanMount` first -- unconditionally, so this is unusable for a
         * puppet -- then builds and queues the rider action and records the
         * horse on the expansion. Answers whether the action was queued, not
         * whether the get-on has finished. `force` is what the game's own
         * `ForceMount` passes set.
         */
        virtual bool StartRide(C_Actor *horse, bool force, bool flagB) = 0;

        /**
         * Begins getting this body out of the saddle. Slot 12, +0x60.
         *
         * Finds the running rider action itself and hands its context to
         * `RequestDismount` with these flags unchanged, and asks nothing of
         * `CanMount`. `force` detaches inline and skips the get-off; `side` is the
         * flank `ChooseDismountSide` (0x180AD1AFC) tries, 1 or 2, with 0
         * taken as 1 -- the ride context records it at +0x09 as side == 1; `place` gates the
         * placement the get-off is anchored to. The engine's own dismount
         * fixes `reason` and `side` at 0.
         */
        virtual bool StopRide(bool force, bool reason, std::uint8_t side, bool place) = 0;

        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /**
         * The horse `StartRide` last put this body on.
         *
         * Written by `StartRide` and **not cleared by `StopRide`**, so it is
         * "the last horse mounted", never "the horse being ridden now". Ask
         * the animal's own rider-sync for that.
         */
        SDK::EntityId GetLastMountedHorse() const {
            return m_lastMountedHorseEntityId;
        }

      private:
        C_ActionActor *m_pActionActor;            // 0x08
        SDK::EntityId m_lastMountedHorseEntityId; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_RiderExpansion, m_pActionActor) == 0x08, "C_RiderExpansion::m_pActionActor offset mismatch");
            static_assert(offsetof(C_RiderExpansion, m_lastMountedHorseEntityId) == 0x10, "C_RiderExpansion::m_lastMountedHorseEntityId offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
