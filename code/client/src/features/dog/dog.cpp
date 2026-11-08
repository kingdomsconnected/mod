#include "dog.h"
#include "dog_service.h"
#include "features/companion/companion_possession.h"

#include "features/player/player.h"
#include "features/npc/npc_locomotion_service.h"

#include "common/module_support.h"
#include "common/spawn/spawn_budget.h"
#include "common/vector_math.h"

#include "sdk/cryengine/action/c_tag_definition.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/entitysystem/i_ai_object.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_anim_action_ai_movement.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_soul_group.h"
#include "sdk/wh/rpgmodule/e_soul_derived_stat.h"
#include "sdk/wh/xgenaimodule/activitysystem/c_dog_companion.h"
#include "sdk/wh/xgenaimodule/activitysystem/dog_companion_mode.h"
#include "sdk/wh/xgenaimodule/c_npc_manager.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>

#include <mafianet/get_time.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace KCDC::Features::Dog {
    namespace {
        constexpr const char *kLogger = "Dog";
        constexpr std::uint8_t kNoTag = Shared::Entities::DogEntity::kNoTag;

        /** How long a retryable spawn failure is left alone before another try. */

        // Where the ground is looked for, either side of the wire position.
        // Generous downwards: the ground under the owner may drop away.
        constexpr float kGroundProbeUp   = 2.0F;
        constexpr float kGroundProbeDown = 8.0F;

        /** How far the animal may stray before the spawn correction lapses. */
        constexpr float kGroundedRadius = 0.25F;

        // Morale is a float the engine recomputes on every read, so two
        // answers a frame apart can differ in the last bit. Comparing them
        // exactly would report the same dog forever.
        constexpr float kMoraleEpsilon = 0.01F;

        /**
         * `companion_playerDog`, the one brain in the game carrying a type-8
         * `DogCompanion` subbrain.
         *
         * `Libs/Tables/ai/brain.xml` names it 4cacb48f-6ebb-71c5-331d-179b9769809b,
         * `brain2subbrain.xml` gives it the single subbrain
         * `companion_playerDog`, and `subbrain.xml` marks that one
         * `subbrain_type="8"` -- the kind the dog activity owner is found by.
         * Exactly one soul in the shipped game asks for it, and it is not one
         * a KCDC body can resolve, so the mod installs the brain itself.
         */
        constexpr SDK::CryGUID kCompanionPlayerDogBrain {0x71C56EBB4CACB48FULL, 0x9B8069979B171D33ULL};

        /** How often a dog with no brain says so. */
        constexpr MafiaNet::Time kBrainReportIntervalMs = 2000;

        bool SameMorale(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < kMoraleEpsilon;
        }
    } // namespace

    // --- Replica3 hooks ---------------------------------------------------

    void Dog::OnConstructed() {
        Framework::Logging::GetLogger(kLogger)->info("Dog replica constructed: networkId={}, soul='{}', owner={}, local={}", GetNetworkID(), soulGuid, ownerNetworkId, IsOwner());
    }

    void Dog::OnDeserialized(bool transformUpdated) {
        // While this client owns the animal it is the one simulating it;
        // drawing it a delay behind would put it behind its own brain.
        if (IsOwner()) {
            return;
        }

        const bool locomotionChanged = !_receivedLocomotionKnown || moveSpeedTag != _receivedMoveSpeedTag || moveDirTag != _receivedMoveDirTag || stanceTag != _receivedStanceTag;
        if (locomotionChanged) {
            _receivedLocomotionKnown = true;
            _receivedMoveSpeedTag    = moveSpeedTag;
            _receivedMoveDirTag      = moveDirTag;
            _receivedStanceTag       = stanceTag;
        }

        // A dog that sits stops moving, so its transform stops changing and
        // no further pose reaches the buffer presentation samples from. A tag
        // change without a transform is therefore held live until a pose
        // carrying it catches up.
        if (!transformUpdated) {
            if (locomotionChanged) {
                _stateOnlyLocomotionPending   = true;
                _stateOnlyLocomotionReleaseAt = 0;
            }
            return;
        }

        // Tags taken here rather than at draw time: the state channel was
        // deserialized in this same call, so it is the one moment the tags and
        // the transform are known to belong together.
        KCDC::Common::Replication::PoseSnapshot pose;
        pose.position     = position;
        pose.velocity     = velocity;
        pose.rotation     = rotation;
        pose.moveSpeedTag = moveSpeedTag;
        pose.moveDirTag   = moveDirTag;
        pose.stanceTag    = stanceTag;
        _snapshots.Push(pose, lastUpdateTime);
        if (_stateOnlyLocomotionPending && _stateOnlyLocomotionReleaseAt == 0) {
            _stateOnlyLocomotionReleaseAt = MafiaNet::GetTime() + static_cast<MafiaNet::Time>(std::ceil(_snapshots.EffectiveDelayMs())) + 1;
        }
    }

    void Dog::OnStateForced() {
        // Nothing to arm. The fields the override carries -- owner, mode, name
        // -- are read by the next frame's reconcile pass, and the dog's pose
        // belongs to whoever is simulating it.
    }

    void Dog::DeallocReplica(MafiaNet::Connection_RM3 *) {
        Despawn();
        delete this;
    }

    // --- The native body --------------------------------------------------

    void Dog::RefreshNativeBinding() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *actorSystem  = SDK::IActorSystem::GetInstance();
        if (_entityId == 0 || entitySystem == nullptr || actorSystem == nullptr) {
            _entity = nullptr;
            _dog    = nullptr;
            return;
        }

        _entity     = entitySystem->GetEntity(_entityId);
        auto *actor = _entity != nullptr ? actorSystem->GetActor(_entityId) : nullptr;

        // Asked rather than assumed: the entity id outlives the body across a
        // level change, and the id the engine hands back next may belong to
        // something else entirely.
        const bool isDog = actor != nullptr && actor->GetActorClass() == SDK::wh::entitymodule::C_Dog::ActorClassId;
        _dog             = isDog ? static_cast<SDK::wh::entitymodule::C_Dog *>(actor) : nullptr;
    }

    void Dog::EnsureCompanionBrain() {
        if (_brainInstalled || _dog == nullptr) {
            return;
        }

        // Already thinking: either the game gave it a companion brain or a
        // previous frame installed one.
        if (GetObjectiveSet() != nullptr) {
            _brainInstalled = true;
            return;
        }

        auto *soul    = _dog->GetSoul();
        auto *manager = SDK::wh::xgenaimodule::C_NPCManager::GetInstance();
        if (soul == nullptr || manager == nullptr) {
            return;
        }

        // The NPC exists from the moment the entity passed the spawn sink, but
        // the body is not up on the first frame, so this simply retries.
        const SDK::EntityId entityId = _entityId;
        auto *npc                    = manager->GetNPCByEntityId(entityId);
        if (npc == nullptr) {
            return;
        }

        void *brain = SDK::wh::xgenaimodule::BuildBrainForGuid(kCompanionPlayerDogBrain, npc);
        if (brain == nullptr) {
            _brainInstalled = true;
            Framework::Logging::GetLogger(kLogger)->error("Dog {}: the companion_playerDog brain template did not resolve; the animal will not think", GetNetworkID());
            return;
        }

        // The soul is told too, so anything re-reading it agrees with the
        // brain the NPC is actually running.
        soul->SetBrainGuid(kCompanionPlayerDogBrain);
        npc->SetBrain(brain);
        _brainInstalled = true;
        Framework::Logging::GetLogger(kLogger)->info("Dog {} given the companion_playerDog brain", GetNetworkID());
    }

    /**
     * Says once whether this dog has a brain, and where it stopped if not.
     *
     * The three stages fail identically from outside -- an animal that simply
     * stands there -- and are fixed in three different places, so each is
     * named. The AI object is reported alongside because "the engine never
     * gave this body an AI" and "it has one but the activity side does not
     * know about it" are the two halves of the same silence.
     */
    void Dog::ReportBrain() {
        if (_companionReported) {
            return;
        }

        auto *soul      = _dog != nullptr ? _dog->GetSoul() : nullptr;
        void *companion = GetObjectivePlanner();
        if (companion != nullptr) {
            _companionReported = true;
            Framework::Logging::GetLogger(kLogger)->info("Dog {} has a native brain at {}", GetNetworkID(), companion);
            return;
        }

        // Rate-limited: a dog that never gets one would otherwise write a line
        // every frame for as long as it exists.
        const auto now = MafiaNet::GetTime();
        if (now < _nextBrainReport) {
            return;
        }
        _nextBrainReport = now + kBrainReportIntervalMs;

        auto *aiObject = _entity != nullptr ? SDK::GetEntityAIObject(_entity) : nullptr;

        // The brain GUID is what the NPC manager looks the brain template up
        // by, and a soul whose brain does not name the DogCompanion subbrain
        // is a dog that cannot think. An all-zero one means the soul asks for
        // no brain at all, which is a different problem in a different place.
        const SDK::CryGUID *brain = soul != nullptr ? soul->GetBrainGuid() : nullptr;
        // Which soul the body actually ended up with. The spawner asks for one
        // by GUID through Properties.guidSharedSoulId; if the name here is not
        // the one that was asked for, the request never took and everything
        // downstream is reasoning about the class default instead.
        const char *soulName = soul != nullptr ? soul->GetName() : nullptr;

        Framework::Logging::GetLogger(kLogger)->warn("Dog {} has no brain yet: soulName='{}', wantedSoul='{}', companion={}, aiObject={}, brainGuid={:016x}-{:016x}", GetNetworkID(), soulName != nullptr ? soulName : "<null>", soulGuid, static_cast<const void *>(companion),
            static_cast<const void *>(aiObject), brain != nullptr ? brain->hipart : 0ULL, brain != nullptr ? brain->lopart : 0ULL);
    }

    /**
     * This dog's brain, through the game's own resolver.
     *
     * Deliberately not `_dog + 0x18`: the object whose `+0x18` the objective
     * paths call through is the AI-side activity owner the NPC manager hands
     * back for a soul, not the entity-side actor. Reaching into the actor
     * there calls through its own opaque prefix and takes the process down.
     */
    SDK::wh::xgenaimodule::activitysystem::C_DogCompanion *Dog::GetCompanion() const {
        auto *soul = _dog != nullptr ? _dog->GetSoul() : nullptr;
        return soul != nullptr ? SDK::wh::xgenaimodule::activitysystem::C_DogCompanion::ResolveForSoul(soul) : nullptr;
    }

    SDK::wh::xgenaimodule::activitysystem::C_DogObjectiveSet *Dog::GetObjectiveSet() const {
        auto *companion = GetCompanion();
        return companion != nullptr ? companion->GetObjectiveSet() : nullptr;
    }

    SDK::wh::xgenaimodule::activitysystem::C_DogObjectivePlanner *Dog::GetObjectivePlanner() const {
        auto *companion = GetCompanion();
        return companion != nullptr ? companion->GetObjectivePlanner() : nullptr;
    }

    // --- Per-frame --------------------------------------------------------

    void Dog::Frame() {
        if (KCDC::Core::gApplication == nullptr) {
            return;
        }

        if (_entityId == 0) {
            RequestSpawn();
            return;
        }

        RefreshNativeBinding();
        if (_entity == nullptr) {
            // The body went with a level change; the next tick re-spawns it.
            // What was buffered describes a world that is no longer loaded.
            WithdrawLocomotion();
            ReconcileSuppression();
            _entityId          = 0;
            _dog               = nullptr;
            _grounded          = false;
            _appliedMode       = kUnappliedMode;
            _companionReported = false;
            _brainInstalled    = false;
            _snapshots.Clear();
            _stateOnlyLocomotionPending   = false;
            _stateOnlyLocomotionReleaseAt = 0;
            _receivedLocomotionKnown      = false;
            return;
        }

        // Before the ownership branches: whose dog this is, what it is called
        // and whether it can think are true of the animal whoever happens to
        // be running it.
        ReconcileSoul();
        EnsureCompanionBrain();
        ReconcileSuppression();

        if (IsOwner()) {
            ReconcileMode();
            CaptureLocalState();
            return;
        }

        PresentRemoteState();
    }

    // --- Capture: this client owns the dog --------------------------------

    /**
     * Reads the pose and the intent of a dog this client runs.
     *
     * Nothing is written onto the body: the owner's own game is simulating it
     * -- its planner, its objectives, its morale -- and this only copies out
     * what that simulation produced.
     */
    void Dog::CaptureLocalState() {
        // The engine animates a dog whose brain is running; the override is
        // for bodies nothing local is driving.
        WithdrawLocomotion();

        // What the buffer holds belongs to a pose this client stopped
        // following when it took the animal over.
        _snapshots.Clear();
        _stateOnlyLocomotionPending   = false;
        _stateOnlyLocomotionReleaseAt = 0;
        _receivedLocomotionKnown      = false;

        const glm::vec3 captured = KCDC::Common::ToGlm(_entity->GetWorldPos());
        if (!KCDC::Common::IsFinite(captured)) {
            return;
        }

        // The velocity the animal's own animation was driven by, which is the
        // movement request's and not a derivative of where it has been. The
        // MoveSpeed tag beside it comes from that same request, so the two
        // only stay in step on a receiver if both are the owner's own.
        velocity = KCDC::Common::ToGlm(_dog->GetMovementVelocity());
        if (!KCDC::Common::IsFinite(velocity)) {
            velocity = glm::vec3(0.0F);
        }
        position = captured;
        rotation = KCDC::Common::ToGlm(_entity->GetWorldRotation());
        CaptureLocomotionTags();
        CaptureIntent();
    }

    /**
     * Reads the locomotion tags the local engine chose for this dog.
     *
     * The owner's engine has already turned the animal's motion into a pace,
     * and its answer is two small numbers that hold steady between a walk, a
     * trot and a run. Re-deriving that from an observed speed would be
     * guessing at boundaries the game itself draws.
     */
    void Dog::CaptureLocomotionTags() {
        auto *tags = _dog != nullptr ? _dog->GetTagState() : nullptr;
        if (tags == nullptr) {
            return;
        }

        const auto read = [this, tags](SDK::wh::entitymodule::C_Actor::EAnimationTagId group) -> std::uint8_t {
            const std::uint32_t tag = tags->GetGroupTag(_dog->GetAnimationTagId(group));
            return tag == SDK::CTagDefinition::kInvalidId || tag > 0xFEU ? kNoTag : static_cast<std::uint8_t>(tag);
        };

        moveSpeedTag = read(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_SPEED);
        moveDirTag   = read(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_DIR);
        stanceTag    = read(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_STANCE);

        // And the state whole, for every group the three above do not name.
        static_assert(Shared::Entities::DogEntity::kTagStateBytes == KCDC::Features::Npc::LocomotionOverride::kTagStateBytes, "The dog's wire tag state and the locomotion override's must be the same width");
        const std::uint8_t *raw = tags->GetRawState();
        for (std::size_t i = 0; i < Shared::Entities::DogEntity::kTagStateBytes; ++i) {
            this->tagState[i] = raw[i];
        }
    }

    /**
     * Declares what the dog is doing, for receivers to present.
     *
     * Morale only, for now: the running objective is read off the companion,
     * which this prototype keeps as an opaque identity. What travels is a
     * report, never a simulation -- morale advances from the local frame delta
     * inside the game's own OwnedDog buff, and no other peer can age it.
     */
    void Dog::CaptureIntent() {
        auto *soul = _dog != nullptr ? _dog->GetSoul() : nullptr;
        if (soul == nullptr) {
            return;
        }

        // What the planner settled on this frame. Every posture a dog has is
        // one of these, so it is the whole of what an observer needs to
        // reproduce a sit, a lie-down or a bark.
        auto *planner = GetObjectivePlanner();
        auto *set     = GetObjectiveSet();
        auto *running = planner != nullptr ? planner->GetRunningObjective() : nullptr;

        // Asked by elimination: an objective carries no key of its own, so
        // every id is resolved and the one that matches wins. The fallback
        // entry answers for several ids, so a shared pointer is not an answer.
        std::uint8_t found = static_cast<std::uint8_t>(SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Invalid);
        if (running != nullptr && set != nullptr) {
            constexpr std::size_t kCount = static_cast<std::size_t>(SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Invalid);
            std::size_t matches          = 0;
            std::size_t matched          = kCount;
            for (std::size_t i = 0; i < kCount; ++i) {
                if (set->GetObjective(static_cast<SDK::wh::xgenaimodule::activitysystem::E_DogObjective>(i)) == running) {
                    ++matches;
                    matched = i;
                }
            }
            if (matches == 1) {
                found = static_cast<std::uint8_t>(matched);
            }
        }

        // And which variant of it. Read live rather than remembered, so a dog
        // that re-rolls while waiting reports the new posture.
        const auto *behaviour = running != nullptr && found == static_cast<std::uint8_t>(SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Wait) ? running->GetWaitBehaviour() : nullptr;
        behaviourFlags        = behaviour != nullptr ? behaviour->GetType() : 0;

        objective = found;

        const float value = soul->GetDerivedStatValue(SDK::wh::rpgmodule::E_SoulDerivedStat::Morale);
        if (!std::isfinite(value) || SameMorale(value, _reportedMorale)) {
            return;
        }

        morale          = value;
        _reportedMorale = value;
    }

    // --- Presentation: somebody else owns the dog -------------------------

    void Dog::PresentRemoteState() {
        KCDC::Common::Replication::PoseSnapshot pose = SampleRenderPose();

        pose.position.z = GroundedHeight(pose.position);
        if (!KCDC::Common::IsFinite(pose.position)) {
            return;
        }

        // Placed rather than pushed, and uncontested: the planner behind this
        // body is suppressed and the tag pass has taken its transform away
        // from the animated character, so the pose stated here is the pose it
        // keeps.
        KCDC::Common::PlaceEntity(_entity, pose.position, pose.rotation);

        // Placed above, animated here -- but only while the animal is doing
        // nothing but moving.
        //
        // A posture is a `C_DogActionAnim` the objective queued on the dog's
        // action queue, and the locomotion override is a standing idle forced
        // into the same body: `SelectState` answers from the override and
        // `CAnimActionAIMovement::Update` re-installs with `trumpSelf`, so the
        // idle wins every frame and the sit is never seen. The override stays
        // while the wire says the animal moves, and stands aside when its own
        // objective has something better to do with the body. The transform is
        // placed above either way.
        //
        // A praise stands the override aside for the same reason, and the
        // animal is asked rather than remembered: the clip is an action on its
        // own queue, which the engine puts there and takes out again.
        const bool bonding = _dog->GetActionActor()->IsInSharedAnimation();
        if (bonding || (IsPostureObjective(static_cast<SDK::wh::xgenaimodule::activitysystem::E_DogObjective>(_presentedObjective)) && pose.moveSpeedTag == kNoTag)) {
            WithdrawLocomotion();
        }
        else {
            PublishLocomotion(pose);
        }

        // And the posture, which is not a pose at all but an objective.
        PresentObjective();
    }

    bool Dog::Praise(SDK::EntityId partner, std::uint32_t variant) {
        return _dog != nullptr && _dog->Bond(partner, variant);
    }

    /**
     * Whether an objective is only a posture -- something the dog does to
     * itself, where it already stands.
     *
     * The rest are excluded on purpose. Follow and its variants move the dog
     * and the wire already carries where it went; Fetch and Eat reach for an
     * item, which on a second client is a second set of hands on it; Teleport
     * and Runaway write the transform outright.
     *
     * Mark looks like a posture and is not one. It is a fun-on-target
     * objective, and those never pick their own target on activation: their
     * slot 24 answers "already have one", trusting the planner's
     * `ComputePriority` to have written it a moment before. A suppressed
     * planner never scores, so activating Mark here hands `OnActivate` a null
     * target, which it dereferences at `0x1809F45BC` -- every client near a
     * marking dog went down at once. Dig is not an objective at all: the set
     * has no Dig entry and answers it with the Wait fallback.
     */
    bool Dog::IsPostureObjective(SDK::wh::xgenaimodule::activitysystem::E_DogObjective objective) {
        switch (objective) {
        case SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Wait:
        case SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Bark:
        case SDK::wh::xgenaimodule::activitysystem::E_DogObjective::ShakeOut: return true;
        default: return false;
        }
    }

    void Dog::RegisterWaitBehaviour(SDK::wh::xgenaimodule::activitysystem::C_DogObjective *objective) {
        auto &service = KCDC::Core::gApplication->GetDogService();
        if (_waitObjective != nullptr && _waitObjective != objective) {
            service.ForgetWaitBehaviour(_waitObjective);
        }
        _waitObjective = objective;
        service.WantWaitBehaviour(objective, behaviourFlags);
    }

    void Dog::ForgetWaitBehaviour() {
        if (_waitObjective == nullptr || KCDC::Core::gApplication == nullptr) {
            _waitObjective = nullptr;
            return;
        }
        KCDC::Core::gApplication->GetDogService().ForgetWaitBehaviour(_waitObjective);
        _waitObjective = nullptr;
    }

    void Dog::PresentObjective() {
        auto *planner = GetObjectivePlanner();
        auto *set     = GetObjectiveSet();
        if (planner == nullptr || set == nullptr) {
            return;
        }

        const auto wanted = static_cast<SDK::wh::xgenaimodule::activitysystem::E_DogObjective>(objective);
        if (!IsPostureObjective(wanted)) {
            _presentedObjective = static_cast<std::uint8_t>(SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Invalid);
            return;
        }

        auto *target = set->GetObjective(wanted);
        if (target == nullptr) {
            _presentedObjective = objective;
            return;
        }

        auto *running = planner->GetRunningObjective();
        if (target == running) {
            _presentedObjective = objective;

            // Right objective, possibly the wrong posture: both clients roll
            // on their own timers, so the owner's answer is imposed as it
            // changes rather than waited for.
            if (wanted == SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Wait) {
                RegisterWaitBehaviour(target);
                if (behaviourFlags != _presentedBehaviour) {
                    const auto *current = target->GetWaitBehaviour();
                    if (behaviourFlags != 0 && (current == nullptr || current->GetType() != behaviourFlags)) {
                        target->SelectNextWaitBehaviour();
                    }
                    _presentedBehaviour = behaviourFlags;
                }
            }
            return;
        }

        // Before the activation, because `OnActivate` is what picks the
        // behaviour. It almost always picks nothing here -- the posture
        // arrives seconds later on a re-select -- which is why the branch
        // above restates it.
        if (wanted == SDK::wh::xgenaimodule::activitysystem::E_DogObjective::Wait) {
            RegisterWaitBehaviour(target);
        }

        // Guarded: the native call reads the running objective without
        // checking it first.
        if (running != nullptr) {
            planner->DeactivateRunningObjective();
        }
        target->Activate(planner);
        _presentedObjective = objective;
        _presentedBehaviour = behaviourFlags;
    }

    /**
     * The pose to draw this frame.
     *
     * Drawn from the two packets bracketing this moment rather than from the
     * newest: the raw pose steps at the send rate, and carrying the newest one
     * forward moves the animal at the jitter of the network.
     */
    KCDC::Common::Replication::PoseSnapshot Dog::SampleRenderPose() {
        KCDC::Common::Replication::PoseSnapshot pose;
        bool poseHeld = false;
        if (!_snapshots.Sample(KCDC::Common::Replication::PoseRenderTime(_snapshots), pose, &poseHeld)) {
            pose.position     = position;
            pose.velocity     = velocity;
            pose.rotation     = rotation;
            pose.moveSpeedTag = moveSpeedTag;
            pose.moveDirTag   = moveDirTag;
            pose.stanceTag    = stanceTag;
        }
        if (_stateOnlyLocomotionPending && _stateOnlyLocomotionReleaseAt != 0 && MafiaNet::GetTime() >= _stateOnlyLocomotionReleaseAt) {
            _stateOnlyLocomotionPending   = false;
            _stateOnlyLocomotionReleaseAt = 0;
        }
        if (_stateOnlyLocomotionPending) {
            pose.moveSpeedTag = moveSpeedTag;
            pose.moveDirTag   = moveDirTag;
            pose.stanceTag    = stanceTag;
        }
        if (poseHeld) {
            KCDC::Common::Replication::HoldLocomotion(pose);
        }
        return pose;
    }

    /**
     * The Z to draw `wanted` at.
     *
     * The height resolved at spawn stands in for the server's inherited one,
     * and lapses once the wire has walked the animal off that spot -- from
     * then on the server's Z is a real answer.
     */
    float Dog::GroundedHeight(const glm::vec3 &wanted) {
        if (!_grounded) {
            return wanted.z;
        }
        if (glm::distance(glm::vec2(wanted), glm::vec2(_groundedPosition)) > kGroundedRadius) {
            _grounded = false;
            return wanted.z;
        }
        return _groundedPosition.z;
    }

    void Dog::PublishLocomotion(const KCDC::Common::Replication::PoseSnapshot &pose) {
        const std::uint32_t state = pose.moveSpeedTag == kNoTag ? SDK::wh::entitymodule::CAnimActionAIMovement::STATE_IDLE : SDK::wh::entitymodule::CAnimActionAIMovement::STATE_MOVEMENT;
        KCDC::Core::gApplication->GetNpcLocomotionService().PublishLocomotionOverride(_entityId, state, pose.moveSpeedTag, pose.moveDirTag, pose.stanceTag, pose.velocity, glm::normalize(pose.rotation), tagState);
    }

    void Dog::WithdrawLocomotion() {
        // The body is the engine's again, transform included: an animal handed
        // back to its own brain must not keep an animated character that
        // refuses to move it.
        if (_dog != nullptr) {
            if (auto *animatedCharacter = _dog->GetAnimatedCharacterRuntime()) {
                animatedCharacter->SetNoMovementOverride(false);
            }
        }

        if (_entityId != 0 && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetNpcLocomotionService().PublishLocomotionOverride(_entityId, 0, kNoTag, kNoTag, kNoTag, glm::vec3(0.0F), glm::identity<glm::quat>());
        }
    }

    // --- The soul ---------------------------------------------------------

    std::string Dog::LocalizationKey() {
        // One word: the native UI '@'-prefixes each word separately, so a key
        // containing a space would resolve as two keys and fail both.
        return "kcdc_dog_" + std::to_string(GetNetworkID());
    }

    void Dog::ReconcileSoul() {
        auto *soul = _dog != nullptr ? _dog->GetSoul() : nullptr;
        if (soul == nullptr) {
            return;
        }

        ReconcileName(soul);
        ReconcilePossession(soul);
    }

    void Dog::ReconcileName(SDK::wh::rpgmodule::C_Soul *soul) {
        if (name.empty()) {
            return;
        }

        const std::string key = LocalizationKey();
        const char *current   = soul->GetUIName();
        if (current != nullptr && key == current) {
            return;
        }

        soul->SetUIName(key.c_str());
        Framework::Logging::GetLogger(kLogger)->info("Dog {} answers to '{}' as '{}'", GetNetworkID(), key, name);
    }

    /**
     * Makes this body its owner's companion, on every client.
     *
     * Possession is what the world reasons about: an unpossessed dog is a
     * stray, which NPCs are authored to notice, and the crime kinds that
     * attribute a dog's violence to a person key on the same master. Souls are
     * per-client objects, so nothing here travels -- each peer states it for
     * itself from the replicated owner id.
     *
     * `PossessAnimal` refuses an animal that already has a master, so a
     * hand-over is release-then-possess rather than possess-over.
     */
    void Dog::ReconcilePossession(SDK::wh::rpgmodule::C_Soul *soul) {
        switch (KCDC::Features::Companion::ReconcileMaster(soul, KCDC::Features::Companion::ResolvePlayerSoul(ownerNetworkId))) {
        case KCDC::Features::Companion::PossessionChange::Possessed:
            Framework::Logging::GetLogger(kLogger)->info("Dog {} possessed by player {}", GetNetworkID(), ownerNetworkId);
            break;
        case KCDC::Features::Companion::PossessionChange::Released:
            Framework::Logging::GetLogger(kLogger)->info("Dog {} released from its previous master", GetNetworkID());
            break;
        case KCDC::Features::Companion::PossessionChange::Refused:
            Framework::Logging::GetLogger(kLogger)->warn("Dog {} refused possession by player {}", GetNetworkID(), ownerNetworkId);
            break;
        case KCDC::Features::Companion::PossessionChange::None:
            break;
        }
    }

    /**
     * Keeps this dog's brain switched off unless this client owns it.
     *
     * One register on the service, one detour on the planner. Everything else
     * about the animal keeps running: it is possessed, animated, physical, and
     * NPCs perceive it -- it just stops deciding, which is the one thing a
     * body placed from the network must not do.
     */
    void Dog::ReconcileSuppression() {
        // Keyed on the planner, because that is the pointer
        // `UpdateObjectivePlanner` is called with. The objective set -- the
        // other object out of the same provider -- never matches.
        auto *planner = IsOwner() ? nullptr : GetObjectivePlanner();
        if (planner == _suppressedPlanner) {
            return;
        }

        auto &service = KCDC::Core::gApplication->GetDogService();
        if (_suppressedPlanner != nullptr) {
            service.ResumePlanner(_suppressedPlanner);
        }
        if (planner != nullptr) {
            service.SuppressPlanner(planner);
            Framework::Logging::GetLogger(kLogger)->info("Dog {} runs on player {}'s client; its planner is suppressed here", GetNetworkID(), ownerNetworkId);
        }
        _suppressedPlanner = planner;
    }

    /**
     * Tells the game which mode this dog is on.
     *
     * The replicated field is only a number until somebody states it: the
     * server writes it, every peer carries it, and this is the one place it
     * reaches the engine. Without this a dog sits in whatever mode a bare
     * possession left it in, which is why `/dog mode follow` used to change
     * nothing at all.
     *
     * Reported alongside the companion pointer the first time a body comes up,
     * because "the brain does not exist" and "the brain is in the wrong mode"
     * look identical from outside and are fixed in different places.
     */
    void Dog::ReconcileMode() {
        ReportBrain();

        if (mode == _appliedMode) {
            return;
        }

        SDK::wh::xgenaimodule::activitysystem::SetPlayerDogMode(static_cast<SDK::wh::xgenaimodule::activitysystem::E_DogCompanionMode>(mode));
        _appliedMode = mode;
        Framework::Logging::GetLogger(kLogger)->info("Dog {} put on mode {}", GetNetworkID(), mode);
    }

    // --- Lifetime ---------------------------------------------------------

    /**
     * Grounds the wire position before the body is created.
     *
     * The server has no engine and places a dog at its owner's own Z, which
     * floats it wherever the ground under the two is not level.
     */
    glm::vec3 Dog::ResolveSpawnPlacement() {
        glm::vec3 placement = position;
        auto *physicalWorld = SDK::CPhysicalWorld::GetInstance();
        if (physicalWorld == nullptr) {
            return placement;
        }

        SDK::Vec3 ground {};
        if (physicalWorld->ResolveGround(KCDC::Common::ToNative(position), kGroundProbeUp, kGroundProbeDown, ground)) {
            placement.z       = ground.z;
            _groundedPosition = placement;
            _grounded         = true;
        }
        return placement;
    }

    void Dog::RequestSpawn() {
        // One request in flight at a time; the spawner calls back when it runs.
        if (_spawnRefused || _spawnTicket != nullptr) {
            return;
        }

        const auto now = MafiaNet::GetTime();
        if (now < _nextSpawnAttempt) {
            return;
        }
        _nextSpawnAttempt = now + KCDC::Common::Spawn::SpawnRetryDelayMs(GetNetworkID());
        _grounded         = false;

        // Validate before the ground query reaches native physics.
        if (!KCDC::Common::IsFinite(position)) {
            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cannot spawn dog {}: position is not finite", GetNetworkID());
            return;
        }

        KCDC::Common::Spawn::SpawnRequest request;
        request.actorClass = SDK::ActorClassNames::Dog;
        request.soulGuid   = soulGuid;
        request.world      = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(ResolveSpawnPlacement()), KCDC::Common::ToNative(rotation)};

        _spawnTicket = KCDC::Core::gApplication->GetActorSpawner().Request(request, [this](KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
            OnSpawned(ticket, spawned);
        });
    }

    void Dog::OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
        // The ticket has run either way, so it never fires again.
        _spawnTicket = nullptr;

        if (!spawned) {
            if (KCDC::Common::Spawn::IsRetryableSpawnResult(ticket.GetResult())) {
                // Reported once rather than once a frame: a level load would
                // otherwise cost a log line every tick it takes to finish.
                if (!_spawnDeferred) {
                    _spawnDeferred = true;
                    Framework::Logging::GetLogger(kLogger)->info("Holding the spawn of dog {}: {}", GetNetworkID(), KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
                }
                return;
            }

            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Could not spawn dog {} (soul '{}'): {}", GetNetworkID(), soulGuid, KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
            return;
        }

        _entityId = ticket.GetEntityId();
        RefreshNativeBinding();
        if (_dog == nullptr) {
            Despawn();
            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Rolled back dog {}: its entity registered no dog actor", GetNetworkID());
            return;
        }

        // Let the native manager schedule wakeup for the initialized dog.
        _dog->RequestPutAway(false);
        _spawnDeferred    = false;
        _nextSpawnAttempt = 0;

        Framework::Logging::GetLogger(kLogger)->info("Spawned dog {} (soul '{}') as entity {}", GetNetworkID(), soulGuid, _entityId);
    }

    void Dog::ReleaseSpawnTicket() {
        if (_spawnTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().Return(_spawnTicket);
        }
        _spawnTicket = nullptr;
    }

    void Dog::Despawn() {
        // Dropped first, so a body that was still queued is never made for a
        // dog that has already gone.
        ReleaseSpawnTicket();

        if (_entityId == 0) {
            return;
        }

        RefreshNativeBinding();

        // Released before the body goes, or its master keeps a companion
        // pointer into an animal that no longer exists.
        if (_dog != nullptr) {
            KCDC::Features::Companion::ReleaseFromMaster(_dog->GetSoul());
        }

        // The brain stops being ours to suppress the moment the body goes,
        // and the objective it was holding stops existing with it.
        ForgetWaitBehaviour();
        if (_suppressedPlanner != nullptr && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetDogService().ResumePlanner(_suppressedPlanner);
            _suppressedPlanner = nullptr;
        }

        WithdrawLocomotion();
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().DespawnNow(_entityId);
            Framework::Logging::GetLogger(kLogger)->info("Despawned dog {} (entity {})", GetNetworkID(), _entityId);
        }
        _entityId          = 0;
        _entity            = nullptr;
        _dog               = nullptr;
        _appliedMode       = kUnappliedMode;
        _companionReported = false;
        _brainInstalled    = false;
        _snapshots.Clear();
        _stateOnlyLocomotionPending   = false;
        _stateOnlyLocomotionReleaseAt = 0;
        _receivedLocomotionKnown      = false;
    }
} // namespace KCDC::Features::Dog
