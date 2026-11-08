#pragma once

#include "shared/features/dog/dog_entity.h"

#include "common/replication/pose_snapshot.h"
#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_dog.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/xgenaimodule/activitysystem/c_dog_companion.h"
#include "sdk/wh/xgenaimodule/activitysystem/c_dog_objective.h"

#include <mafianet/replica_manager3.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Features::Dog {
    /**
     * Client half of a replicated dog companion.
     *
     * Every client spawns the body and possesses it onto its owner's soul, so
     * the world treats it as somebody's dog rather than a stray -- that is
     * what the `dog_companion` classification, `GetCompanionMaster` and the
     * `assaultByDog` crime kind all key on. What differs is who decides:
     * on the owner's client the game's own objective planner runs the animal
     * and this replica only reads it; everywhere else the planner is switched
     * off and the pose comes off the wire.
     *
     * `DogEntity` is shared with the server, which has no engine, so the
     * native body this replica is bound to is held here rather than there.
     */
    class Dog final: public Shared::Entities::DogEntity {
      public:
        // --- Replica3 hooks ---

        void OnConstructed() override;

        /**
         * A packet for this dog landed.
         *
         * A pose that came with it is buffered rather than presented, so the
         * animal is drawn from the two packets that bracket the moment.
         */
        void OnDeserialized(bool transformUpdated) override;

        /**
         * The server overrode this dog's state.
         *
         * Only the owner is spoken to, and unlike a horse nothing warps: the
         * server never moves a dog, so the fields are adopted and the animal
         * stays where its own engine put it. The possession pass picks the new
         * owner up on the next frame.
         */
        void OnStateForced() override;

        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- The native body ---

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /**
         * Plays a praise between this dog and `partner`, the praising
         * player's body on this client, with the variant the owner rolled.
         *
         * `C_Dog::Bond` takes its partner from the *local* player, which is the
         * one thing that stops the native being called outright -- it would
         * pair this client's own character with somebody else's dog. So the
         * SDK's `Bond` takes the partner instead. The gate the native runs
         * first (`CanBondNow`: distance, facing, height between the local
         * player and the dog) is not repeated; the owner's client answered it
         * about the two bodies that matter.
         */
        bool Praise(SDK::EntityId partner, std::uint32_t variant);

        /** The live entity, or nullptr while the body is not up. */
        SDK::IEntity *GetGameEntity() const {
            return _entity;
        }

        /** The live dog actor, or nullptr while the body is not up. */
        SDK::wh::entitymodule::C_Dog *GetDog() const {
            return _dog;
        }

        /**
         * This dog's activity companion -- the object whose planner decides
         * what it does next -- or nullptr while the body is not up.
         *
         * An identity rather than something to read through: the suppression
         * register tells one dog's brain from another's by this pointer.
         */
        /** This dog's brain, or nullptr when its soul's brain data names no companion subbrain. */
        SDK::wh::xgenaimodule::activitysystem::C_DogCompanion *GetCompanion() const;

        /** The objective set: what `GetObjective` must be asked of. Provider slot 3. */
        SDK::wh::xgenaimodule::activitysystem::C_DogObjectiveSet *GetObjectiveSet() const;

        /** The planner: what an objective is activated against. Provider slot 7. */
        SDK::wh::xgenaimodule::activitysystem::C_DogObjectivePlanner *GetObjectivePlanner() const;

        /**
         * Re-reads the native body behind this replica's entity id.
         *
         * The binding is a frame-scoped memo: `Frame` refreshes it for the
         * tick. Entity ids outlive bodies across a level change and are handed
         * out again, so the actor is asked what class it is rather than
         * assumed to still be the dog this replica spawned.
         */
        void RefreshNativeBinding();

        /** Whether the body is up, after refreshing the binding. */
        bool IsSpawned() const {
            return _dog != nullptr;
        }

        // --- Per-frame ---

        /** This dog, this frame: capture if we own it, present it if we do not. */
        void Frame();

        // --- Naming ---

        /**
         * The localization key this dog's name answers to.
         *
         * A name in KCD2 is a localization key, never text: the soul's
         * `m_uiName` is loaded from `skald_character.ui_name_string_name` and
         * the native UI '@'-prefixes every word of it before resolving it. The
         * mod writes a key of its own instead and answers it at the engine's
         * own lookup, exactly as a horse does.
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

        /** Takes this body out of the locomotion override table. */
        void WithdrawLocomotion();

      private:
        void CaptureLocalState();
        void PresentRemoteState();

        /** Reads the locomotion tags the local engine chose for this dog. */
        void CaptureLocomotionTags();

        /** Reads the running objective and the dog's mood off the native brain. */
        void CaptureIntent();

        /**
         * Runs the owner's objective on this client's copy of the animal.
         *
         * A replicated dog has its planner suppressed, so nothing starts an
         * objective on it -- and every posture a dog has is an objective
         * queueing a `C_DogActionAnim` on its action queue. The tags those
         * actions set are a symptom, so copying them onto a body running no
         * action does nothing; the objective itself is what has to travel.
         * This makes the same two calls the planner makes.
         */
        void PresentObjective();

        /** Follows the owner's wait behaviour on `objective`, withdrawing any earlier one. */
        void RegisterWaitBehaviour(SDK::wh::xgenaimodule::activitysystem::C_DogObjective *objective);

        /** Withdraws this body's wait behaviour entry. */
        void ForgetWaitBehaviour();

        /** Whether an objective is only a posture, and so safe to replay. */
        static bool IsPostureObjective(SDK::wh::xgenaimodule::activitysystem::E_DogObjective objective);

        /**
         * States this dog's possession and its name on the soul.
         *
         * Stated every frame rather than latched, so a body that streams in
         * late, a soul that is rebuilt, or an owner whose copy spawns a
         * frame after the dog is simply told again.
         */
        void ReconcileSoul();
        void ReconcileName(SDK::wh::rpgmodule::C_Soul *soul);
        void ReconcilePossession(SDK::wh::rpgmodule::C_Soul *soul);

        /** Says once whether this dog has a brain, and where it stopped if not. */
        void ReportBrain();

        /**
         * Gives this dog the companion brain, if the game did not.
         *
         * A KCDC-spawned body never resolves the shared soul it was asked for:
         * the entity falls back to `Dog.lua`'s `defaultSoulClass = "dog"` and
         * builds a blank soul named after the entity, whose brain is
         * `animal_basic`. That brain has no type-8 `DogCompanion` subbrain, so
         * the animal has no companion object and nothing to decide with -- it
         * simply stands there, however well it is possessed.
         *
         * So the brain is rebuilt here from the one template in the game that
         * carries that subbrain, exactly the way the NPC record initialiser
         * builds it, and set on the NPC. Run on every client: an observer's
         * dog needs the companion to exist so its planner can be suppressed by
         * identity, and so the body animates like a dog rather than a statue.
         */
        void EnsureCompanionBrain();

        /** Whether this client's planner suppression matches who owns the dog. */
        void ReconcileSuppression();

        /**
         * States the replicated companion mode on the native dog.
         *
         * Owner-only: the game's own setter resolves the *local* player's
         * primary companion by itself, so this needs nothing from the brain.
         * Latched on the mode, and the latch is cleared whenever the body goes
         * -- the native call queues an acknowledging bark every time, so an
         * unchanged mode must not restate it.
         */
        void ReconcileMode();

        /** The pose to draw this frame, sampled from between two packets. */
        KCDC::Common::Replication::PoseSnapshot SampleRenderPose();

        /** The Z to draw at, honouring the height resolved at spawn. */
        float GroundedHeight(const glm::vec3 &wanted);

        /** Publishes what this body should be doing, for the locomotion hooks. */
        void PublishLocomotion(const KCDC::Common::Replication::PoseSnapshot &pose);

        /** Grounds the wire position before the body is created. */
        glm::vec3 ResolveSpawnPlacement();

        SDK::EntityId _entityId            = 0;
        SDK::IEntity *_entity              = nullptr;
        SDK::wh::entitymodule::C_Dog *_dog = nullptr;

        // The companion this replica last registered with the service, so a
        // brain that is already suppressed is not registered again every frame
        // and a body that was rebuilt releases the pointer it used to hold.
        void *_suppressedPlanner = nullptr;

        // The wait behaviour last imposed, so the owner's choice is
        // re-imposed when it changes rather than every frame.
        std::uint8_t _presentedBehaviour = 0;

        // The wait objective this body registered a wanted behaviour against,
        // so the entry goes when the body does rather than outliving it and
        // imposing a posture on whatever reuses the allocation.
        SDK::wh::xgenaimodule::activitysystem::C_DogObjective *_waitObjective = nullptr;
        std::uint8_t _presentedObjective                                      = static_cast<std::uint8_t>(SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Invalid);

        // What was last stated to the game, and the companion it was stated
        // to. kUnappliedMode is not an E_DogCompanionMode value, so the first
        // frame after the body is up always applies.
        static constexpr std::uint8_t kUnappliedMode = 0xFF;
        std::uint8_t _appliedMode                    = kUnappliedMode;

        // Set once the native brain has been reported, so the log carries one
        // line per body rather than one per frame.
        bool _companionReported = false;
        bool _brainInstalled    = false;

        MafiaNet::Time _nextBrainReport = 0;

        // Remote-only: the tags the state channel last carried, and whether
        // a change to them is still waiting for a transform to travel beside.
        bool _receivedLocomotionKnown                = false;
        std::uint8_t _receivedMoveSpeedTag           = 0;
        std::uint8_t _receivedMoveDirTag             = 0;
        std::uint8_t _receivedStanceTag              = 0;
        bool _stateOnlyLocomotionPending             = false;
        MafiaNet::Time _stateOnlyLocomotionReleaseAt = 0;

        // Set when the spawn failed for a reason retrying cannot fix, so the
        // failure is not re-reported every frame.
        bool _spawnRefused               = false;
        MafiaNet::Time _nextSpawnAttempt = 0;

        // Set once the spawn has been reported as waiting on the world, so a
        // level load costs one log line rather than one per frame.
        bool _spawnDeferred = false;

        KCDC::Common::Spawn::SpawnTicket *_spawnTicket = nullptr;

        // Where the body was grounded at spawn. The server has no engine and
        // places a dog at its owner's own Z, which floats it wherever the
        // ground under the two is not level. Only the Z is taken from here.
        glm::vec3 _groundedPosition = glm::vec3(0.0F);
        bool _grounded              = false;

        // The morale this client last told the server about, so a value the
        // report is already in flight for is not sent again every frame while
        // it makes the round trip.
        float _reportedMorale = -1.0F;

        // Remote-only: what the owner has told us, drawn from between two
        // packets rather than from the newest one carried forward -- which is
        // extrapolation, and turns network jitter into the gait.
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> _snapshots {&KCDC::Common::Replication::PoseBufferConfig()};
    };
} // namespace KCDC::Features::Dog
