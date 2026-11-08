#pragma once

#include "core/feature_registry.h"

#include "horse.h"

#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_horse_ride_context.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_soul_death_record.h"

#include <mafianet/types.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Horse {
    /**
     * Everything about horses that is not about one horse.
     *
     * The seat register, the local player's ride and the saddle the server
     * refused are all single-instance state the engine's hooks write into and
     * the tick reads back. It lives here, on one object with a lifetime, so
     * that the replicas stay replicas.
     */
    class HorseService final: public KCDC::Core::IFeatureService {
      public:
        /** Game thread: drives every live horse, then reports mounts. */
        void Update() override;

        /**
         * Session ended: forgets the ride and the whole seat register.
         *
         * _rpcsInstalled is not cleared. The handlers belong to the peer,
         * which outlives the session, and registering them again would answer
         * every mount reply twice.
         */
        void Reset() override;

        /** Forgets what the engine last said about the local player's ride. */
        void ForgetLocalMount();

        // Clears the reported saddle without sending a dismount after despawn
        // or server rejection.
        void ForgetReportedSaddle(std::uint64_t horseNetworkId);

        // --- Ending a ride ---

        /**
         * Takes `rider` out of the saddle now and sets them down beside it:
         * Lua's `ForceDismount`, the game's own immediate get-off.
         *
         * It refuses when both flanks are blocked, and then the ride's state
         * machine is forced into dismounting, which places the rider on the
         * side it last chose and refuses nothing. Either way the ride settles
         * on none, the rider action finishes, and its `Exit` unwinds the rest
         * -- the sitting pose and the seat -- on the rider's next action
         * update, not here.
         */
        static void EndRide(SDK::wh::entitymodule::C_Actor *rider);

        /**
         * The engine is declaring `record`'s death, from its own dispatch.
         * Returns true when the body is in a saddle -- a rider, or a horse
         * carrying one -- and the death is held: the dispatch must not run.
         *
         * The game has no death in the saddle. `HandleDeath` never touches
         * a ride, and the only way off a horse it has is the get-off of a
         * living pair, whose last steps -- stopping the sitting pose,
         * releasing the seat -- run from the rider action's `Exit` a frame
         * later. A death played in between leaves the rider standing in the
         * saddle pose. So the ride ends first, the natural way, and the death
         * is handed back to the engine's dispatch once `DetachFromHorse` has
         * released the seat.
         */
        bool HoldDeathInSaddle(const SDK::wh::rpgmodule::S_SoulDeathRecord &record);

        /** Whether `horseEntityId` is a replicated horse that has died. */
        bool IsDeadHorse(SDK::EntityId horseEntityId) const;

        /**
         * Whether `bodyEntityId` is a copy of a horse another client runs.
         * Its knockout is its simulator's, which the copy wears
         * (`Horse::PresentLife`); a blow this client deals it goes to the
         * simulator (`QueueHit`), so this client's own verdict on the blow
         * neither knocks the copy out nor plays the knockout's fall on it.
         */
        bool IsHorseCopy(SDK::EntityId bodyEntityId) const;

        /** Whether `buff` is the knockout a blow gives (`unconscious`) and `soul` a horse copy's. */
        bool RefusesKnockout(const SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID &buff) const;

        // --- Life ---

        /** A blow the local player dealt a copy of a horse this client does not run, for the server to hand on. */
        void QueueHit(std::uint64_t horseNetworkId, float amount, std::uint8_t reason);

        /** What the soul of a horse this client runs took, for the server to announce. */
        void QueueDamage(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason);

        /** The soul of a horse this client runs died, for the server to announce. */
        void QueueDeath(std::uint64_t horseNetworkId, std::uint64_t killerNetworkId, std::uint8_t reason);

        // --- Engine edges, raised by this feature's own hooks ---

        /**
         * The engine's ride state machine moved for `riderEntityId`.
         *
         * Only recorded: the report goes out from the next Update, so nothing
         * is sent from inside the state machine itself.
         */
        void NotifyRideState(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId, std::uint8_t rideState, std::uint8_t dismountSide);

        /** The flank the last rider of this horse got off on in its own game, 0 if unknown. */
        std::uint8_t DismountSideOf(SDK::EntityId horseEntityId) const;

        /**
         * `horseEntityId` took `riderEntityId` into its saddle, or lost
         * whoever was in it when that is 0. Kept here because the property
         * the engine keeps it in lives on the animal, not on the rider.
         */
        void NotifySeat(SDK::EntityId horseEntityId, SDK::EntityId riderEntityId);

        /**
         * The horse this body is in the saddle of, or 0.
         *
         * True from the moment the ride attaches to the moment it detaches.
         * A queued rider action is not a seat and does not show up here.
         */
        SDK::EntityId SeatOf(SDK::EntityId riderEntityId) const;

        /**
         * Drops any seat naming `entityId`, on either side of it. Entity ids
         * are reused, so a body that leaves without the engine unseating it
         * would otherwise hand its saddle to whoever gets that id next.
         */
        void ForgetSeatsFor(SDK::EntityId entityId);

        /** Drops every seat, for a world that is going away. */
        void ForgetSeats();

        // --- Mounts in flight ---
        //
        // Queueing a rider action is not a seat: the queue enters it on the
        // rider's next action update, and `C_ActorActionRider::Enter` either
        // attaches the pair or refuses and drops the action. Until one of
        // those happens a second mount must not be queued -- asked again every
        // frame, the queue piles up rider actions for a body that is not
        // updating -- and after a refusal the next one waits for the thing that
        // refused it to change: the bodies becoming ready, or waking up after
        // a native refusal.

        /** The character and animation controller native mounting requires. */
        static bool IsBodyReadyToRide(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Whether a mount of `riderEntityId` onto `horseEntityId` was queued
         * and has not been answered, or is still waiting after a refusal.
         * Releases a readiness refusal once both bodies can enter the ride.
         */
        bool IsMountOutstanding(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId);

        /**
         * A rider action for `horseEntityId` is being queued on
         * `riderEntityId`. Noted before the queue is asked, since it may enter
         * the action -- and seat or refuse the rider -- inside that call.
         */
        void NoteMountQueued(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId);

        /** The queue would not take the rider action after all: nothing is in flight. */
        void ForgetMountInFlight(SDK::EntityId riderEntityId);

        /**
         * The rider action's entry refused `riderEntityId` the saddle of
         * `horseEntityId`, and the queue dropped it. A seat taken in the same
         * call is not a refusal; see `NotifySeat`.
         */
        void NotifyMountRefused(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId, bool waitingForBodies);

        /**
         * `actor` is being put away -- from the engine's put-away manager, or
         * its own immediate request. Returns true when the transition must be
         * held: the body is a horse with somebody in its saddle.
         *
         * Putting an actor away drops its render proxy, and with it every
         * entity slot, so the horse is left with no character in slot 0. The
         * rider's `C_ActorActionRider::Exit` then reaches
         * `C_RiderSync::DetachFromHorse`, which reads that character without
         * asking, and faults. The game never puts a ridden horse away -- its
         * dormancy follows the player -- but a copy ridden by a remote player
         * far from this one is exactly what the soul LOD sends to sleep. The
         * horse is held awake until the saddle is empty, and the engine's
         * request is then handed back to it.
         */
        bool HoldPutAway(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * `entityId` finished waking up: its model setup has run, so a horse
         * has a character again. Any mount refused on either side of it may
         * be queued again.
         */
        void NotifyActorWoken(SDK::EntityId entityId);

        /** Drives a riderless navigation request until ForgetHorse. Game thread only. */
        void Drive(SDK::wh::entitymodule::C_Horse *horse, SDK::CMovementRequest &request);
        void ForgetHorse(SDK::EntityId horseEntityId);

        /** Allows only our request while navigation owns a riderless controller. Any thread. */
        bool OnHorseMovementRequest(SDK::EntityId entityId, const SDK::CMovementRequest &request) const;

        // --- Mount jumps ---

        /**
         * A horse left the ground (`Horse::CaptureLocalState`). Counted only
         * under this machine's player, for the horse the wire reports the
         * jumps of. Game thread.
         */
        void NotifyMountJumped(SDK::wh::entitymodule::C_Horse *horse);

        /**
         * The reactions the horse under this machine's player made -- ride
         * states 4 (a quick start), 5 (pulling up from a walk), 6 (a hard
         * stop), 7 (rearing) and 8 (a throw), each entered from another
         * state -- counted as the jumps are, for the horse's
         * `reactionSequence`, and the ride states of the newest, four bits
         * each with the newest lowest, for its `reactions`.
         */
        std::uint8_t LocalMountReactions() const {
            return _localMountReactions;
        }
        std::uint32_t LocalMountReactionHistory() const {
            return _localMountReactionHistory;
        }

        /** How many jumps this machine's player's mounts have made, wrapping. */
        std::uint8_t LocalMountJumps() const {
            return _localMountJumps;
        }

        // --- Names ---

        /**
         * The replicated name behind a `kcdc_horse_<id>` key, or nullptr.
         *
         * Borrowed from the replicated string, so good only for the duration
         * of the call. Game thread only, like anything that walks replication.
         */
        static const char *ResolveName(std::uint64_t networkId);

      private:
        /** Registers the client's half of the mount conversation, once. */
        void InstallRPCs();

        /** A blow the server handed over from another player; see `HorseHit`. */
        void ApplyForwardedHit(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason);

        /** Climbs the local player onto the horse a scripted mount named, once the body can. */
        void UpdatePendingMount();

        /** Sends what the soul hook queued: hits for the server to hand on, damage for it to announce. */
        void FlushReports();

        /** Hands every held death whose saddle is empty back to the engine's dispatch. */
        void ReleaseHeldDeaths();

        /** Hands every put-away held for a saddle that has since emptied back to the engine. */
        void ReleaseHeldPutAways();

        /**
         * Seats every remote body on the horse the wire says it is riding.
         *
         * Reconciled rather than edge-driven: stating the answer every frame
         * is self-healing, so a body that streams in late, respawns after a
         * level change or misses a transition still ends up on the right
         * animal.
         */
        void UpdateRiders();

        /**
         * Tells the server the local player got into or out of a saddle.
         * Edge-triggered; the standing answer is the replicated rider.
         */
        void UpdateLocalMount();

        // Reports saddle changes. Switching horses sends dismount before
        // mount and transfers authority to the new rider.
        void PublishSaddleChange(std::uint64_t horseNetworkId);

        /** Tells the server about one saddle transition. */
        void ReportHorseMount(std::uint64_t horseNetworkId, bool mounted);

        /** The replicated horse the engine currently has us on, or 0. */
        std::uint64_t MountedNetworkId() const;

        /** Drops every seat `matches` names, on either side of it. */
        template <typename Predicate>
        void EraseSeatsWhere(Predicate &&matches);

        // Who is in whose saddle, rider entity to horse entity, as the engine
        // last said. Written by the seat hook only, and for every horse in the
        // world rather than only the replicated ones.
        std::unordered_map<SDK::EntityId, SDK::EntityId> _seats;

        // The local player's ride as the engine's state machine last left it,
        // written by the hook and read once a tick for the saddle edges.
        SDK::EntityId _rideHorseEntityId = 0;
        std::uint8_t _rideState          = SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE;
        std::uint8_t _dismountSide       = 0;

        /** A horse the server refused us, which we are getting back off. */
        std::uint64_t _deniedHorseId = 0;

        // A scripted mount waiting on the body to be able to take it, and when
        // it gives up: the horse may still be streaming in.
        std::uint64_t _pendingMountHorseId = 0;
        bool _pendingMountInstant          = false;

        // The server order the local player last climbed on through, so the
        // seat it reports says how it was taken: straight in for an instant
        // order, the get-on for anything else.
        std::uint64_t _orderedMountHorseId   = 0;
        bool _orderedMountInstant            = false;
        MafiaNet::Time _pendingMountDeadline = 0;
        MafiaNet::Time _nextMountAttempt     = 0;

        // What the soul hook saw, sent from the next tick rather than from
        // inside the engine's own write. One struct serves both directions:
        // a hit is ours to hand on, damage is our soul's to announce.
        struct PendingDamage {
            std::uint64_t horseNetworkId    = 0;
            std::uint64_t attackerNetworkId = 0;
            float amount                    = 0.0F;
            std::uint8_t reason             = 0;
        };
        std::vector<PendingDamage> _pendingHits;
        std::vector<PendingDamage> _pendingDamage;
        std::vector<PendingDamage> _pendingDeaths;

        // Deaths the dispatch was about to play on a body still in a saddle,
        // with the body that wore the soul: an entity id is what survives
        // the body going away, where the soul pointer would not.
        struct HeldDeath {
            SDK::wh::rpgmodule::S_SoulDeathRecord record;
            SDK::EntityId bodyEntityId = 0;
        };
        std::vector<HeldDeath> _heldDeaths;

        // One mount in flight per rider, rider entity to the horse it was
        // queued for, and whether the entry refused it since.
        struct MountInFlight {
            SDK::EntityId horseEntityId = 0;
            bool refused                = false;
            bool waitingForBodies       = false;
        };
        std::unordered_map<SDK::EntityId, MountInFlight> _mountsInFlight;

        // Horses the engine asked to put away while somebody was in the
        // saddle, and those whose saddle has emptied since, to be handed back
        // from the next tick rather than from inside the detach itself.
        std::vector<SDK::EntityId> _heldPutAways;
        std::vector<SDK::EntityId> _releasedPutAways;

        // Last reported saddle, retained so dismount can identify the horse
        // being left.
        std::uint64_t _reportedHorseId = 0;

        // Jumps made under this machine's player; see NotifyMountJumped.
        std::uint8_t _localMountJumps = 0;
        // Reactions under this machine's player; see NotifyRideState.
        std::uint8_t _localMountReactions        = 0;
        std::uint32_t _localMountReactionHistory = 0;

        struct HorseSlot {
            std::atomic<SDK::EntityId> entityId {0};
        };
        const HorseSlot *FindSlot(SDK::EntityId horseEntityId) const;
        HorseSlot *FindSlot(SDK::EntityId horseEntityId);
        HorseSlot *ClaimSlot(SDK::EntityId horseEntityId);
        static constexpr std::size_t kMaxHorseSlots = 64;
        std::array<HorseSlot, kMaxHorseSlots> _horseSlots {};
        bool _slotsFullReported = false;
        std::atomic<const SDK::CMovementRequest *> _drivingRequest {nullptr};

        bool _rpcsInstalled = false;
    };
} // namespace KCDC::Features::Horse
