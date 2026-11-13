#pragma once
#include "features/npc/npc_movement_progress.h"

#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_buff.h"

#include "common/replication/body_life.h"
#include "common/replication/pose_snapshot.h"
#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <mafianet/replica_manager3.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Horse {
    /**
     * One received horse pose: the body pose every replicated body carries,
     * and how many jumps and reactions the animal had made by then, with
     * the ride states of the newest reactions. The counts are discrete, so
     * they come from the newer of two blended poses, as the body's tags do.
     */
    struct HorsePoseSnapshot {
        KCDC::Common::Replication::PoseSnapshot body;
        std::uint8_t jumpSequence     = 0;
        std::uint8_t gaitCommand      = 0;
        std::uint8_t pace             = 0;
        std::uint8_t reactionSequence = 0;
        std::uint32_t reactions       = 0;
    };

    /** How two horse poses combine: the body as any body's does, the jump count from the newer. */
    struct HorsePoseSnapshotPolicy {
        static glm::vec3 Position(const HorsePoseSnapshot &snapshot) {
            return snapshot.body.position;
        }

        static glm::vec3 Velocity(const HorsePoseSnapshot &snapshot) {
            return snapshot.body.velocity;
        }

        static HorsePoseSnapshot Blend(const HorsePoseSnapshot &a, const HorsePoseSnapshot &b, float alpha, float spanMs) {
            HorsePoseSnapshot out = b;
            out.body              = KCDC::Common::Replication::PoseSnapshotPolicy::Blend(a.body, b.body, alpha, spanMs);
            return out;
        }

        static HorsePoseSnapshot Extrapolate(const HorsePoseSnapshot &newest, float dtSeconds) {
            HorsePoseSnapshot out = newest;
            out.body              = KCDC::Common::Replication::PoseSnapshotPolicy::Extrapolate(newest.body, dtSeconds);
            return out;
        }
    };

    /**
     * Client half of a replicated horse.
     *
     * Every client spawns the body, the eventual rider included: a horse
     * nobody can walk up to is a horse nobody can mount. What differs is who
     * runs it. One client owns it -- the rider, or with the saddle empty the
     * nearest player the server elected -- and that client's game simulates
     * the animal: its pose flows upstream, and its soul is the one whose
     * health and death are the horse's. Every other copy is drawn from what
     * that client reports, and only reproduces its death.
     *
     * `HorseEntity` is shared with the server, which has no engine, so the
     * native body this replica is bound to is held here rather than there.
     */
    class Horse final
        : public Shared::Entities::HorseEntity
        , public KCDC::Common::Replication::IReplicatedLife {
      public:
        // --- Replica3 hooks ---

        void OnConstructed() override;

        /**
         * A packet for this horse landed.
         *
         * A pose that came with it is buffered rather than presented, so the
         * animal is drawn from the two packets that bracket the moment.
         */
        void OnDeserialized(bool transformUpdated) override;

        /**
         * The server overrode this horse's state. Only reaches the client
         * running it, where a scripted move has to land as a warp -- and only
         * a move: a push made for the rider or the life carries the pose too,
         * a round trip old.
         */
        void OnStateForced() override;

        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- The native body ---

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** The live entity, or nullptr while the body is not up. */
        SDK::IEntity *GetGameEntity() const {
            return _entity;
        }

        /** The live horse actor, or nullptr while the body is not up. */
        SDK::wh::entitymodule::C_Horse *GetHorse() const {
            return _horse;
        }

        /** Whoever is in this horse's saddle locally, or nullptr. */
        SDK::wh::entitymodule::C_Actor *GetRider() const;

        /**
         * Re-reads the native body behind this replica's entity id.
         *
         * The binding is a frame-scoped memo: `Frame` refreshes it for the
         * tick, and anything reaching a horse from outside the tick -- an RPC
         * handler, say -- refreshes the one it is about first. Entity ids
         * outlive bodies across a level change and are handed out again, so
         * the actor is asked what class it is rather than assumed to still be
         * the horse this replica spawned.
         */
        void RefreshNativeBinding();

        /** Whether the body is up, after refreshing the binding. */
        bool IsSpawned() const {
            return _horse != nullptr;
        }

        /**
         * Whether this machine saw the current rider take the saddle: the
         * seat filled while this horse's body stood here. A ride the horse
         * streamed in with, or that began before its body was up, was not.
         */
        bool WasMountWitnessed() const {
            return _mountWitnessed;
        }

        // --- Per-frame ---

        /** This horse, this frame: capture if we ride it, present it if we do not. */
        void Frame();

        /**
         * Whether the local player is the one in this saddle.
         *
         * Read from the rider-sync property, which is the answer the engine
         * itself trusts: its own mount check refuses an animal that has one.
         */
        bool IsRiddenByLocalPlayer() const;

        /** Whether the saddle holds another player's native body, whose own rider input drives this copy. */
        bool IsRiddenByRemotePlayer() const;

        // --- Naming ---

        /**
         * The localization key this horse's name answers to.
         *
         * A horse's name in KCD2 is a localization key, never text: the soul's
         * `m_uiName` is loaded from `skald_character.ui_name_string_name`, and
         * the native UI '@'-prefixes every word of it before resolving it, so
         * "Pebbles" written there renders as "@Pebbles". The mod writes a key
         * of its own instead and answers it at the engine's own lookup.
         *
         * The key carries the NetworkID rather than the name, so nothing is
         * stored: the lookup reads the replicated field back.
         */
        std::string LocalizationKey();

        // --- Lifetime ---

        /**
         * Asks the spawner for a body, at most one request in flight. The body
         * arrives in `OnSpawned` on a later frame.
         */
        void RequestSpawn();
        void OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned);

        /** Cancels a request that has not run, or forgets one that has. */
        void ReleaseSpawnTicket();
        void Despawn();

        // --- Life (IReplicatedLife) ---

        std::uint64_t LifeNetworkId() const override {
            return const_cast<Horse *>(this)->GetNetworkID();
        }

        /** The client simulating the horse decides its life; every other copy mirrors it. */
        bool DecidesOwnLife() const override {
            return IsOwner();
        }

        /** Handed to the simulator through the server as a `HorseHit`. */
        void OnLocalBlow(float amount, std::uint8_t reason) override;

        /** Reported as `HorseDamaged`, and remembered so a death that follows can name who dealt it. */
        void OnOwnDamage(std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) override;

        /** On the simulator, the death itself: `alive` drops and `HorseDied` goes to the server. */
        void OnSoulDied() override;

        /**
         * Ends any knockout the body's soul is in, the way the game ends one
         * early, and drops the one this body was kept in for the wire: on the
         * simulator at the server's `Wake`, wherever the wire says the horse
         * is up while this body is down, and before the body is removed.
         */
        void Wake();

      private:
        /** Drops the ride this horse arrived with once its rider changes. */

        void CaptureLocalState();
        void Navigate();
        void PresentNavigation();
        void ReleaseController();
        void ResetSettle();
        bool Settle(float gap);
        void PresentRemoteState();

        /** Ends the replay of a remote rider's jumps and the drift correction of its mount. */
        void StopFollowingRider();

        /** Hands the copy's pace and speed back to its own runtime; see `PresentRemoteState`. */
        void StopForcingLocomotion();

        /** Drops the mount from the native-player register once neither the rider's gait nor the locomotion holds it. */
        void ReleaseMountIfIdle();

        /**
         * Reconciles this body's soul with the wire: the name travels down as
         * a key written onto it, the capacity travels up as a number read off
         * it. Stated every frame, so a body that streams in late or has its
         * soul rebuilt is simply told again.
         */
        void ReconcileSoul();
        void ReconcileName(SDK::wh::rpgmodule::C_Soul *soul);
        void ReportCapacity(SDK::wh::rpgmodule::C_Soul *soul);

        /**
         * Makes this body its owner's horse on this client's souls, or
         * nobody's. The game's own notion of whose horse this is: the owner's
         * is never a stolen horse to the NPCs' crime checks, and it is the one
         * the owner's horse paths answer with -- and, on every machine, it is
         * as immortal as the game makes a player's own horse.
         */
        void ReconcilePossession(SDK::wh::rpgmodule::C_Soul *soul);

        /**
         * Reconciles the body's life with the wire. The copy running the
         * horse applies what the server wrote and reports what its soul
         * holds; every other copy plays the knockout, the death and the
         * revival.
         */
        void ReconcileLife();
        void ApplyServerLife(SDK::wh::rpgmodule::C_Soul *soul);
        void CaptureLife(SDK::wh::rpgmodule::C_Soul *soul);
        void PresentLife(SDK::wh::rpgmodule::C_Soul *soul);

        /**
         * Reconciles what the body wears with the wire. A server write this
         * copy has not applied yet dresses it; a change the local rider made
         * at the horse inventory screen is reported; gear the engine took off
         * on its own is put back.
         */
        void ReconcileGear();

        /** Tells the server what this copy wears, unless that report is already in flight. */
        void ReportGear(const std::string &worn);

        /** Dresses the body in the wire's gear; false when it could not be reached yet. */
        bool ApplyServerGear();

        /** Drops what this copy knew about the gear of a body that is gone. */
        void ForgetGear();

        /**
         * Puts the server's buffs on this copy's soul and takes off the ones
         * it no longer lists. Every copy does this, the simulator included:
         * a horse's speed, stamina and courage are read off its own soul, so
         * each machine's copy has to wear them for the horses to agree. A
         * listed effect the soul lost -- it ran out, or the game took it off
         * -- is put back within a second.
         */
        void ReconcileBuffs();

        /** Drops what this copy knew about the buffs of a body that is gone. */
        void ForgetBuffs();

        /** What the body wears now, packed as the wire carries it; false when its inventory is not up. */
        bool CaptureWornGear(std::string &out);

        /** Continues the wire's jump count with the jumps this machine's player makes on this horse. */
        void CaptureJumps();

        /** The pose to draw this frame, sampled from between two packets; `held` says it is a pose past the newest. */
        HorsePoseSnapshot SampleRenderPose(bool *held = nullptr);

        /** The Z to draw at, honouring the height resolved at spawn. */
        float GroundedHeight(const glm::vec3 &wanted);

        /** Grounds the wire position before the body is created. */
        glm::vec3 ResolveSpawnPlacement();

        SDK::EntityId _entityId                = 0;
        SDK::IEntity *_entity                  = nullptr;
        SDK::wh::entitymodule::C_Horse *_horse = nullptr;

        KCDC::Features::Npc::MovementProgress _navigationProgress;
        std::uint32_t _navigationRevision = 0;
        MafiaNet::Time _navigationTick    = 0;
        glm::quat _navigationFacing {1.0F, 0.0F, 0.0F, 0.0F};

        // Set when the spawn failed for a reason retrying cannot fix, so the
        // failure is not re-reported every frame.
        bool _spawnRefused               = false;
        MafiaNet::Time _nextSpawnAttempt = 0;

        // Set once the spawn has been reported as waiting on the world, so a
        // level load costs one log line rather than one per frame.
        bool _spawnDeferred = false;

        KCDC::Common::Spawn::SpawnTicket *_spawnTicket = nullptr;

        // Where the body was grounded at spawn. The server has no engine and
        // places a horse at the caller's own Z, which floats it wherever the
        // ground under the two is not level. Only the Z is taken from here.
        glm::vec3 _groundedPosition = glm::vec3(0.0F);
        bool _grounded              = false;

        // Set by OnStateForced; consumed by the next owner frame.
        bool _forcedPosePending = false;

        // Only riderless navigation owns the controller. Ridden horses use
        // the native player's input path.
        enum class ControllerUse : std::uint8_t {
            None,
            Driving,
            Navigating
        };
        ControllerUse _controllerUse  = ControllerUse::None;
        bool _settleOver              = false;
        float _settleBestGap          = 0.0F;
        MafiaNet::Time _settleCheckAt = 0;

        // A moving copy: the bend its heading is turned off its owner's
        // toward the owner's line, and the owner's facing and the time it was
        // last stated at, which the owner's turn rate is measured from. Zero
        // while the owner stands.
        float _steerBend        = 0.0F;
        float _steerOwnerYaw    = 0.0F;
        MafiaNet::Time _steerAt = 0;

        // A copy: the facing it was last asked to turn to, and when, which
        // the next frame's step carries on from while the body has not
        // turned that far. Zero while nothing has been asked of this body.
        float _headingAsked            = 0.0F;
        MafiaNet::Time _headingAskedAt = 0;

        // A copy: when it was last presented, for the frame's length, and
        // the native body whose pace and speed the hooks hold to the wire's
        // -- kept as a key, so it can be released after the body has gone.
        MafiaNet::Time _presentedAt                        = 0;
        const SDK::wh::entitymodule::C_Actor *_forcedMount = nullptr;

        // A copy: the jump and reaction counts it last played, taken from the
        // pose when a remote rider sat down, so the ones made before are not
        // replayed.
        std::uint8_t _appliedJumpSequence     = 0;
        std::uint8_t _appliedReactionSequence = 0;

        // The rider the wire last named, and whether its taking the saddle
        // was seen here; see WasMountWitnessed.
        std::uint64_t _knownRiderNetworkId = 0;
        bool _mountWitnessed               = false;
        bool _followingRider               = false;

        // A copy: jumps the owner's horse made that it has yet to take.
        std::uint8_t _jumpsOwed = 0;

        // The horse this client runs: whether it was in the air last frame,
        // so a jump is counted where it leaves the ground.
        bool _wasJumping = false;

        // The horse this client runs: what `LocalMountJumps` has to be added
        // to for the wire's count, set when this client took it over.
        std::uint8_t _jumpBase     = 0;
        std::uint8_t _reactionBase = 0;
        bool _capturingJumps   = false;

        // Life: whether this body has been handed the server's life write
        // for the revision it carries. Cleared with the body and with a
        // change of who runs it. Whether it is dead is its soul's to say.
        bool _broughtUp                      = false;
        bool _simulating                     = false;
        bool _lifeApplied                    = false;
        std::uint32_t _appliedVitalsRevision = 0;

        // A copy: whether it is knocked out for its simulator's knockout, and
        // when the unconscious buff may next be put on again if the game
        // dropped it. Cleared with the body, once the wire says the horse is
        // up, and when this machine takes the horse on.
        bool _knockedOut                    = false;
        MafiaNet::Time _nextKnockoutAttempt = 0;

        // The last blow this copy's soul took, for the death it may cause.
        std::uint64_t _lastAttackerNetworkId = 0;
        std::uint8_t _lastBlowReason         = 0;

        // The capacity this client last told the server about, so a value the
        // report is already in flight for is not sent again every frame while
        // it makes the round trip.
        float _reportedCapacity = 0.0F;

        // Gear: the revision this body was last dressed from, and what it wore
        // when this copy last looked -- a difference from that on the rider's
        // copy is the rider changing it. Cleared with the body.
        std::uint32_t _appliedGearRevision = 0;
        std::string _observedGear;

        // The report in flight, so it is not sent again every frame of the
        // round trip.
        std::string _reportedGear;
        std::uint32_t _reportedGearRevision = 0;
        bool _gearReported                  = false;
        MafiaNet::Time _gearReportedAt      = 0;

        // Re-dressing a copy that does not wear what the server wrote, per write.
        std::uint32_t _gearRepairs     = 0;

        // Buffs: the revision this body's soul was last given the server's
        // buffs from and which definitions those were, the instances this copy
        // put on itself by definition and native handle -- the only ones a
        // later write takes off -- and when the soul is next checked for one
        // that went missing. Cleared with the body.
        std::uint32_t _appliedBuffRevision = 0;
        std::vector<Shared::RPC::BuffId> _appliedHorseBuffs;
        std::vector<std::pair<Shared::RPC::BuffId, std::uint64_t>> _ownedHorseBuffs;
        std::uint64_t _nextBuffCheck       = 0;
        std::uint64_t _buffWaitLoggedAt    = 0;
        MafiaNet::Time _nextGearRepair = 0;

        // Remote-only: what the rider has told us, drawn from between two
        // packets rather than from the newest one carried forward -- which is
        // extrapolation, and turns network jitter into the gait.
        Framework::Utils::SnapshotBuffer<HorsePoseSnapshot, HorsePoseSnapshotPolicy> _snapshots {&KCDC::Common::Replication::PoseBufferConfig()};
    };
} // namespace KCDC::Features::Horse
