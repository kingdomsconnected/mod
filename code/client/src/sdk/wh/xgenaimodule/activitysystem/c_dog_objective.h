#pragma once

#include "../../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule::activitysystem {
    /** `E_DogObjective`, the name table at `0x18204F5A0`. */
    enum class E_DogObjective : std::uint8_t {
        Wait        = 0,
        Mark        = 1,
        Bark        = 2,
        Dig         = 3,
        Follow      = 4,
        FollowHeel  = 5,
        FollowRider = 6,
        Search      = 7,
        MeleeCombat = 8,
        Fetch       = 9,
        Hunt        = 10,
        Runaway     = 11,
        Alarm       = 12,
        Fail        = 13,
        Move        = 14,
        FunMove     = 15,
        FunOnSo     = 16,
        FunOnPoint  = 17,
        Chase       = 18,
        Eat         = 19,
        ShakeOut    = 20,
        Distract    = 21,
        Pet         = 22,
        SearchItem  = 23,
        Teleport    = 24,
        Invalid     = 25,
    };

    class C_DogObjectivePlanner;

    /**
     * One entry in a waiting dog's configured behaviour set.
     *
     * Only the type is modelled, because it is the only field the mod reads
     * and the entry's full extent is unproven. `BeginWaitBehavior` switches on
     * it at `0x1809F438C`: 1, 2 and 3 select the postures through the dog's
     * animation providers, and every other value falls to its default case,
     * which is standing there.
     */
    class S_DogWaitBehaviour {
      public:
        std::uint8_t GetType() const {
            return m_type;
        }

      private:
        std::uint8_t m_type; // 0x00

        static void AssertLayout() {
            static_assert(offsetof(S_DogWaitBehaviour, m_type) == 0x00, "S_DogWaitBehaviour::m_type offset mismatch");
        }
    };

    /**
     * One thing a dog can be doing, and the engine's own way to start it.
     *
     * Everything a dog visibly does that is not walking is an objective: the
     * sit and the lie-down are `C_WaitDogObjective` picking a wait behaviour,
     * the bark is `C_BarkDogObjective`. Each queues a `C_DogActionAnim` -- a
     * `C_ActorActionAnim` -- on the animal's own `C_ActionActor`, so a posture
     * is a running *action*, and the Mannequin Stance tag beside it is a
     * consequence rather than a cause. Writing that tag onto a body whose
     * objective never ran does nothing at all.
     *
     * Petting is *not* one of these, whatever `E_DogObjective::Pet` suggests.
     * `C_PetDogObjective` exists, derives from
     * `C_PlayerSynchroAnimBaseDogObjective` (`0x18330CD60`) and would walk the
     * animal to its master and start a shared clip -- but nothing reaches it.
     * `PetDog` (`0x183303548`) is its only route in and has a single caller,
     * the RTTR registration at `0x1810FCAA0`; the targeted interaction
     * (`FindEligibleObjective`) only ever tries the three ids in the table at
     * `0x18492EAC8`, which are Fetch, MeleeCombat and Hunt. Watching
     * `QueueDogObjective`, the choke point every dog command passes through,
     * a praise never appears.
     *
     * What petting really is lives on `C_Dog` instead: `Bond`, one action on
     * the animal's own queue, with the planner left running nothing at all
     * while it plays. See `C_DogExternalActionSyncAnimMaster`.
     */
    class C_DogObjective {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;

        /**
         * Makes this the planner's running objective and starts it.
         *
         * Slot 2, the one the planner calls at `0x1809F6D88`. The thunk behind
         * it swaps the arguments -- `mov rax, rdx / mov rdx, rcx / mov rcx,
         * rax` at `0x1809F7300` -- and the body then does `mov [rcx+10h], rdx`,
         * so the planner takes the objective rather than the other way round,
         * and `C_ActivityObjective::Start` runs on the objective.
         * `OnActivate` is what queues the animal's actions.
         */
        virtual void Activate(C_DogObjectivePlanner *planner) = 0; // slot 2

        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;

        /**
         * One tick of this objective.
         *
         * Slot 10, called from the else-branch of `UpdateObjectivePlanner` at
         * `0x1809F6D9B` once the scoring has settled on keeping the objective
         * already running. Suppressing the planner therefore does not merely
         * stop a dog re-planning, it stops the objective it is running from
         * advancing at all.
         */
        virtual void Update() = 0; // slot 10

        /**
         * Whether this objective is started but still queued behind an
         * occupied activity slot, so its `OnActivate` has not run yet.
         *
         * `C_ActivityObjective::Start` (`0x1809F7670`) parks the slot it waits
         * on here and defers activation to that slot's callback. The planner
         * ticks the running objective only while this is clear -- the
         * `running+0x20 == 0` test before the slot-10 call at `0x1809F6D9B` --
         * so an objective whose activation is still pending is never updated.
         */
        bool IsAwaitingActivitySlot() const {
            return m_pAwaitedActivitySlot != nullptr;
        }

        /**
         * The behaviour a waiting dog settled on, or nullptr for none.
         *
         * Activation settles on nothing; the posture arrives seconds later
         * when the objective re-selects, so a variant read once at activation
         * is always absent.
         */
        const S_DogWaitBehaviour *GetWaitBehaviour() const {
            return m_pWaitBehaviour;
        }

        /** Re-picks the wait behaviour now, rather than on this dog's own timer. */
        void SelectNextWaitBehaviour();

      private:
        SDK::wh::shared::C_Signal m_signal0008; // 0x008, the base ctor (0x1806265D8) uses dispatch unk_18566C040
        void *m_pContext0018;                   // 0x018, slot 0 of the ctor's provider argument
        void *m_pAwaitedActivitySlot; // 0x020
        std::byte m_unset0028[0x38];            // 0x028, not written by the base ctor
        std::uint64_t m_state0060;              // 0x060, base ctor zeroes
        std::uint16_t m_state0068;              // 0x068, base ctor zeroes
        std::byte m_unset006A[0x6];             // 0x06A, not written by the base ctor
        void *m_pProvider0070;                  // 0x070, the ctor's provider argument
        std::byte m_unset0078[0x48];            // 0x078, not written by the base ctor
        const S_DogWaitBehaviour *m_pWaitBehaviour; // 0x0C0

        static void AssertLayout() {
            static_assert(offsetof(C_DogObjective, m_pAwaitedActivitySlot) == 0x20, "C_DogObjective::m_pAwaitedActivitySlot offset mismatch");
            static_assert(offsetof(C_DogObjective, m_pWaitBehaviour) == 0xC0, "C_DogObjective::m_pWaitBehaviour offset mismatch");
            static_assert(offsetof(C_DogObjective, m_pProvider0070) == 0x70, "C_DogObjective::m_pProvider0070 offset mismatch");
        }
    };

    /**
     * The set of objectives a dog has, keyed by `E_DogObjective`.
     *
     * The companion's activity provider hands it out through slot 3, and
     * `FindEligibleObjective` calls `GetObjective` on exactly that -- which is
     * what pins the pairing, since the same call on the planner would read a
     * map that is not there.
     */
    class C_DogObjectiveSet {
      public:
        /**
         * This dog's instance of `objective`, if it has an enabled one.
         *
         * Looked up in the map at `+0x1468`. A key that is absent, disabled or
         * out of order gets a fixed fallback entry rather than null, so the
         * same pointer answers for several keys.
         */
        C_DogObjective *GetObjective(E_DogObjective objective) const;
    };

    /**
     * What decides, each tick, which objective a dog is running.
     *
     * The companion's activity provider hands it out through slot 7, and it is
     * what `UpdateObjectivePlanner` takes as `this`. A replicated dog has that
     * planner suppressed -- it must not decide, because deciding includes
     * teleporting itself -- which leaves it with no objective, and so with
     * none of the actions that make it sit, lie or bark. Driving it to the
     * objective its owner reports is the same two calls the planner makes, in
     * the same order.
     */
    class C_DogObjectivePlanner {
      public:
        /** The objective this dog is running, or nullptr. */
        C_DogObjective *GetRunningObjective() const {
            return m_pRunningObjective;
        }

        /**
         * Interrupts the running objective and leaves the planner idle.
         *
         * Reads the running objective without checking it, so it must not be
         * called with nothing running.
         */
        void DeactivateRunningObjective();

      private:
        std::byte m_unknown0000[0x10];       // 0x000, no constructor located
        C_DogObjective *m_pRunningObjective; // 0x010

        static void AssertLayout() {
            static_assert(offsetof(C_DogObjectivePlanner, m_pRunningObjective) == 0x10, "C_DogObjectivePlanner::m_pRunningObjective offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule::activitysystem
