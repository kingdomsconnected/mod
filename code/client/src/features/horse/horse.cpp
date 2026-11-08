#include "horse.h"
#include "features/companion/companion_possession.h"
#include "horse_service.h"

#include "features/player/player.h"
#include "common/actor_dressing.h"
#include "common/angles.h"
#include "common/module_support.h"
#include "features/player/native_player_module.h"
#include "common/spawn/spawn_budget.h"

#include "common/vector_math.h"

#include "features/buff/custom_buff_module.h"
#include "shared/features/horse/horse_buffs.h"
#include "shared/features/horse/horse_gear.h"
#include "shared/features/horse/horse_stats.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/custom_buff_registry.h"
#include "shared/game/horse_gait.h"
#include "shared/game/horse_gear_catalog.h"
#include "shared/game/progression_catalog.h"

#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_horse_ride_context.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/rpgmodule/c_buff_instance.h"
#include "sdk/wh/rpgmodule/c_buff_manager.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_perk_list.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_unconscious_buff_init_params.h"
#include "sdk/wh/rpgmodule/e_soul_derived_stat.h"
#include "sdk/wh/rpgmodule/e_soul_state.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Horse {
    namespace {
        constexpr const char *kLogger = "Horse";

        // How far a copy may drift from the owner's horse before it is placed
        // there: a gap that size is a lost packet, a warp or a rider not yet
        // in this copy's saddle, not a stride its own simulation will close.
        constexpr float kSnapDistance = 6.0F;

        // How close to the simulator's facing, as the |dot| of two unit quaternions,
        // counts as already facing that way.
        constexpr float kFacingUnchanged = 0.99999F;

        // Below this the navigating horse is standing: the engine's own moving
        // gate, the square root of the 0.01 it compares a velocity against.
        constexpr float kStandSpeed = 0.1F;

        // The move target leads the simulator's position by this much travel, and
        // never by less or more than the two lengths after it. Near enough and
        // the controller brakes into it, stopping and restarting the gait.
        constexpr float kLeadSeconds = 2.0F;
        constexpr float kLeadMin     = 2.0F;
        constexpr float kLeadMax     = 30.0F;

        // How much faster, or slower, than the simulator the copy may be asked to
        // go while it closes a gap along its path.
        constexpr float kCatchUpMax = 1.05F;

        // A standing copy further than this from where the simulated horse
        // stopped walks onto the spot rather than standing beside it -- once
        // per stop, and only while it is still closing the gap: the spot can
        // be one this body cannot reach (the simulated horse pressed into a wall
        // its own collider stops short of), and asking for it anyway leaves
        // the copy walking in place.
        constexpr float kSettleDistance          = 0.5F;
        constexpr float kSettleOvershoot         = 2.5F; // past the controller's arrival radius
        constexpr float kSettleSpeed             = 1.5F;
        constexpr float kSettleFacing            = 0.7F; // cosine: walks once it faces the spot within ~45 degrees, else stands and turns
        constexpr float kSettlePace              = 1.0F; // walk
        constexpr float kSettleProgress          = 0.1F; // metres the gap must shrink by...
        constexpr MafiaNet::Time kSettleWindowMs = 1500; // ...within this long, a turn towards the spot included

        // Below the snap, a copy is kept on its owner's horse through its own
        // movement request, never by a push or a placement. A push went in
        // as forced movement (`ForceMovement`, Normal requests only) and lost
        // most of itself at a gallop, whose travel is the gait's own -- 0.5 of
        // 5.25 m/s arrived, measured in game -- while a braking horse took
        // all of it and ran 5-6 m past its owner's; a position written every
        // frame broke the gait's animation and the jump replay, as it did for
        // remote players. So the gap along the owner's path is made up in the
        // runtime speed the request is built from, closed over this long and
        // by no more than the larger of these two, and the gap across it in
        // the heading, bent toward the owner's line by the angle it subtends
        // over the path ahead (at least kSteerLookAheadMin), never more than
        // kSteerMaxRadians. The turn and the change of pace then come out of
        // the horse's own gait, lean and turn blend.
        constexpr float kFollowSeconds         = 0.5F;
        constexpr float kFollowSpeedDiff       = 1.0F;  // m/s
        constexpr float kFollowSpeedShare      = 0.25F; // of the owner's speed
        constexpr float kSteerLookAheadSeconds = 0.5F;
        constexpr float kSteerLookAheadMin     = 1.0F;  // m
        constexpr float kSteerMaxRadians       = 0.35F; // ~20 degrees

        // Whatever the gap asks, the speed stays inside what the gait's
        // blendspace shows and the bend turns no faster than its turn axis
        // leaves over (`Shared::Game::HorseGaitBand`). The pace is the move
        // style of the runtime speed (ApplyRuntimePaceAndDelta, 0x1806CF3D4)
        // and CryEngine plays the gait at the asked-for travel speed clamped
        // to the blendspace's range, so a speed outside it moved the body
        // faster or slower than its stride, or tipped it into the next gait.
        // A frame longer than this is a stall, not a turn to measure.
        constexpr MafiaNet::Time kSteerMaxFrameMs = 250;

        // Below this the owner's horse stands: it is not steered toward, and
        // a ridden copy within kHaltedResyncGap of it is left where it stands
        // rather than walked a step by wire jitter. A riderless one settles
        // onto the spot instead; see `PresentRemoteState`.
        constexpr float kMinLerpSpeed    = 0.5F;
        constexpr float kHaltedResyncGap = 1.0F; // m

        // The facing is turned toward the owner's at a rate, never written:
        // `ProcessTurning` (0x1809D1D8C) requests the whole difference
        // between the entity and its base every ground frame, so a base yaw
        // written whole was a pivot the clip did not animate -- the feet
        // skidded sideways on every turn and every steer. Moving, the rate
        // is the owner's own turn plus the gait's turn axis
        // (`HorseGaitBand::turnSpeedMax`); standing, the walk's, which is
        // also the turn-in-place. A quarter turn or more is taken whole, as
        // a placement is: no clip plays it either way.
        constexpr float kStandTurnRate        = 2.0F;   // rad/s
        constexpr float kHeadingSnapRadians   = 1.5708F;
        constexpr MafiaNet::Time kMaxFrameMs  = 250;

        // Below this a stamina the simulator reported is the one the copy's soul holds.
        constexpr float kStaminaEpsilon = 1.0F;

        // Jumps the owner's horse made that a copy may still owe; see PresentRemoteState.
        constexpr int kMaxJumpsOwed = 2;

        /** How long a retryable spawn failure is left alone before another try. */

        // Where the ground is looked for, either side of the wire position.
        // Generous downwards: the ground ahead of the caller may drop away.
        constexpr float kGroundProbeUp   = 2.0F;
        constexpr float kGroundProbeDown = 8.0F;

        /** How far the animal may stray before the spawn correction lapses. */
        constexpr float kGroundedRadius = 0.25F;

        // The carrying capacity is recomputed by the engine on every read, so
        // two answers a frame apart can differ in the last bit of a division.
        // Comparing them exactly would report the same horse forever.
        constexpr float kCapacityEpsilon = 0.01F;

        // Below this a health the server asked for is the one the soul holds.
        constexpr float kHealthEpsilon = 0.01F;

        // `unconscious_permanent`: the game's own unconscious buff
        // (`Cpp:Unconscious`, consciousness 0) with no time of its own. A copy
        // wears it for as long as its simulator's horse is knocked out; one
        // that takes the horse on meanwhile trades it for kBlowKnockoutBuff.
        constexpr KCDC::Shared::RPC::BuffId kUnconsciousBuff = KCDC::Shared::Game::BuffIdFromGuid("c75aa0db-65ca-44d7-9001-e4b6d38c6875");

        // `unconscious`: the knockout a blow gives
        // (`AddUnconsciousnessBuffEffect`, 0x182D25AE4, cause 1), which lasts
        // as long as its depth takes to drain by the soul's
        // `UnconsciousnessFadeoutSpeed` (derived stat 56) per real second. A
        // soul the game holds immortal is put kImmortalKnockoutDepth deep
        // (`ResolveKnockout`, 0x180C62F1C):
        // `CombatHitImmortalUnconsciousDepth`, RPG param +0x870, which
        // `rpg_param.xml` sets to 480.
        constexpr KCDC::Shared::RPC::BuffId kBlowKnockoutBuff = KCDC::Shared::Game::BuffIdFromGuid("f8d60fe4-e2c1-420a-946a-213e1cd09264");
        constexpr std::uint32_t kBlowKnockoutCause            = 1;
        constexpr float kImmortalKnockoutDepth                = 480.0F;

        // How long a copy waits before putting the unconscious buff on again
        // when the game dropped it; see `PresentLife`.
        constexpr MafiaNet::Time kKnockoutRetryMs = 500;

        // `remove_unconsciousness`: the game's way to end a knockout early
        // (buff class 16, ClassExclusive, `Cpp:BasicTimed`, duration 0); see
        // `Wake`.
        constexpr KCDC::Shared::RPC::BuffId kWakeBuff = KCDC::Shared::Game::BuffIdFromGuid("bd22f98a-e61f-4d83-b39c-79d1d85b6b91");

        // The master's perk that forwards immortality to its horse, and the
        // perk it forwards (`perk_companion.xml`, companion type 0); see
        // `ReconcilePossession`.
        constexpr KCDC::Shared::RPC::PerkGuid kCompanionHorsePerk       = KCDC::Shared::Game::PerkGuidFromText("9533bcec-0ded-4263-9702-8d54e32ad058");
        constexpr KCDC::Shared::RPC::PerkGuid kCompanionImmortalityPerk = KCDC::Shared::Game::PerkGuidFromText("63067fb0-76d9-40bb-ae6c-d8f7627d24f7");

        // How far a reported health or stamina has to move before the field
        // changes. Both tick every frame -- regeneration, a gallop -- and the
        // field channel sends whatever differs, so an exact copy would put
        // every running horse on the wire at the serialize rate for a number
        // no script needs to the hundredth. Reaching an end is always sent.
        constexpr float kVitalReportStep = 1.0F;

        /** `reported`, moved to `value` once it has gone far enough or hit an end. */
        void ReportVital(float &reported, float value, float maximum) {
            if (!std::isfinite(value)) {
                return;
            }
            value            = std::clamp(value, 0.0F, maximum > 0.0F ? maximum : value);
            const bool atEnd = value <= 0.0F || (maximum > 0.0F && value >= maximum);
            if (std::fabs(value - reported) >= kVitalReportStep || (atEnd && value != reported)) {
                reported = value;
            }
        }

        // How often, and how many times per server write, a copy that lost
        // gear nobody here took off is dressed again.
        constexpr MafiaNet::Time kGearRepairIntervalMs = 1000;
        constexpr std::uint32_t kGearRepairLimit       = 3;

        // How long the rider's copy waits on the answer to a gear report
        // before treating it as lost: the server drops one silently when it
        // does not have the rider in the saddle yet, or the report is stale.
        constexpr MafiaNet::Time kGearReportTimeoutMs = 2000;

        bool SameCapacity(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < kCapacityEpsilon;
        }
    } // namespace

    // --- Replica3 hooks ---------------------------------------------------

    void Horse::OnConstructed() {
        // A horse that arrives with a rider was mounted out of this machine's
        // sight; see WasMountWitnessed.
        _knownRiderNetworkId = riderNetworkId;
        Framework::Logging::GetLogger(kLogger)->info("Horse replica constructed: networkId={}, soul='{}', rider={}, local={}", GetNetworkID(), soulGuid, riderNetworkId, IsOwner());
    }

    void Horse::OnDeserialized(bool transformUpdated) {
        // The seat filling while this body stands in the world here is a
        // mount this machine watched happen; one that arrives with the horse,
        // or before its body is up, is a ride already under way.
        if (riderNetworkId != _knownRiderNetworkId) {
            _mountWitnessed      = _knownRiderNetworkId == 0 && riderNetworkId != 0 && _horse != nullptr;
            _knownRiderNetworkId = riderNetworkId;
        }

        // While this client rides the animal it is the one simulating it;
        // drawing it a delay behind would put it behind its own rider.
        if (!transformUpdated || IsOwner()) {
            return;
        }

        // The jump count taken here rather than at draw time: the state
        // channel was deserialized in this same call, so it is the one moment
        // the count and the transform are known to belong together.
        HorsePoseSnapshot pose;
        pose.body.position = position;
        pose.body.velocity = velocity;
        pose.body.rotation = rotation;
        pose.jumpSequence     = jumpSequence;
        pose.gaitCommand      = gaitCommand;
        pose.pace             = pace;
        pose.reactionSequence = reactionSequence;
        pose.reactions        = reactions;
        _snapshots.Push(pose, GetLastTransformTime());
    }

    void Horse::OnStateForced() {
        // Or'd in: a push for something else arriving before the tick must not
        // cancel a move that has not been taken yet.
        _forcedPosePending = _forcedPosePending || WasPoseForced();
    }

    void Horse::DeallocReplica(MafiaNet::Connection_RM3 *) {
        // A horse that goes while we are on it takes the report with it, or the
        // next dismount would name an entity the server has already forgotten.
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetHorseService().ForgetReportedSaddle(GetNetworkID());
            KCDC::Common::Replication::BodyLife::Get().Unbind(this);
        }
        Despawn();
        delete this;
    }

    // --- The native body --------------------------------------------------

    void Horse::RefreshNativeBinding() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *actorSystem  = SDK::IActorSystem::GetInstance();
        if (_entityId == 0 || entitySystem == nullptr || actorSystem == nullptr) {
            _entity = nullptr;
            _horse  = nullptr;
            return;
        }

        _entity     = entitySystem->GetEntity(_entityId);
        auto *actor = _entity != nullptr ? actorSystem->GetActor(_entityId) : nullptr;

        // Asked rather than assumed: the entity id outlives the body across a
        // level change, and the id the engine hands back next may belong to
        // something else entirely.
        const bool isHorse = actor != nullptr && actor->GetActorClass() == SDK::wh::entitymodule::C_Horse::ActorClassId;
        _horse             = isHorse ? static_cast<SDK::wh::entitymodule::C_Horse *>(actor) : nullptr;
    }

    SDK::wh::entitymodule::C_Actor *Horse::GetRider() const {
        return _horse != nullptr ? _horse->GetRider() : nullptr;
    }

    bool Horse::IsRiddenByLocalPlayer() const {
        auto *rider                       = GetRider();
        const SDK::EntityId localEntityId = KCDC::Common::LocalActorEntityId();
        return rider != nullptr && localEntityId != 0 && rider->GetEntityId() == localEntityId;
    }

    bool Horse::IsRiddenByRemotePlayer() const {
        return KCDC::Features::Player::NativePlayerModule::IsRemote(GetRider());
    }

    // --- Per-frame --------------------------------------------------------

    void Horse::Frame() {
        if (_entityId == 0) {
            RequestSpawn();
            return;
        }

        RefreshNativeBinding();
        if (_entity == nullptr) {
            // The body went with a level change; the next tick re-spawns it.
            // What was buffered describes a world that is no longer loaded.
            ReleaseController();
            StopFollowingRider();
            StopForcingLocomotion();
            ResetSettle();
            KCDC::Common::Replication::BodyLife::Get().Unbind(this);
            _broughtUp           = false;
            _entityId            = 0;
            _horse               = nullptr;
            _grounded            = false;
            _lifeApplied         = false;
            _knockedOut          = false;
            _nextKnockoutAttempt = 0;
            _snapshots.Clear();
            ForgetGear();
            ForgetBuffs();
            return;
        }

        if (!_broughtUp) {
            _broughtUp = KCDC::Common::Replication::BodyLife::BringUp(_horse, _entity);
        }
        if (_horse != nullptr) {
            KCDC::Common::Replication::BodyLife::Get().Bind(_horse->GetSoul(), this);
        }

        // Before the ownership branches, because a horse's name, what it can
        // carry and whether it is alive are true of the animal whoever
        // happens to be driving it.
        ReconcileSoul();
        ReconcileLife();
        ReconcileGear();
        ReconcileBuffs();

        if (IsOwner()) {
            StopFollowingRider();
            StopForcingLocomotion();
            ResetSettle();
            Navigate();
            CaptureLocalState();
            return;
        }
        _capturingJumps = false;
        if (_controllerUse == ControllerUse::Navigating) {
            ReleaseController();
        }

        // A corpse is the engine's: its ragdoll puts it where it falls, and
        // placing it from the wire would fight that every frame. So is a
        // knocked-out copy until it gets up: the wire then carries where the
        // simulator's own ragdoll lies, and `PresentRemoteState` would steer
        // this one's ragdoll toward it, or place it past kSnapDistance.
        // Once it is up it walks onto the spot its simulator's horse stands
        // on, which its own ragdoll did not put it on (`Settle`).
        if (!alive || _knockedOut) {
            ReleaseController();
            StopFollowingRider();
            StopForcingLocomotion();
            ResetSettle();
            _snapshots.Clear();
            return;
        }

        // Ownership only arrives a round trip after the saddle does, and any
        // dismount the server believes takes it away again -- but the animal
        // under the local player is being driven by this game throughout.
        // Placing it from a pose that stale would fight its own rider.
        if (IsRiddenByLocalPlayer()) {
            ReleaseController();
            StopFollowingRider();
            StopForcingLocomotion();
            ResetSettle();
            // What was buffered describes the animal before this client took
            // the reins, and getting off hands it back.
            _snapshots.Clear();
            return;
        }

        if (navigationRevision != 0 && riderNetworkId == 0 && _horse != nullptr && !_horse->HasRider()) {
            StopFollowingRider();
            StopForcingLocomotion();
            PresentNavigation();
            return;
        }
        ReleaseController();
        PresentRemoteState();
    }

    // --- Presentation: somebody else runs the animal ----------------------

    /**
     * Keeps a copy with the horse its owner runs.
     *
     * Nothing steers the copy from here. With another player's body in the
     * saddle it is driven as the owner's is: that body's PreUpdate hands the
     * replayed stick to its C_RiderPlayerInput (the movement-input listeners
     * at C_PlayerInput+0x2C0), whose Update builds the gait, steering and
     * brake (0x180A4F304), and the tags follow from the runtime speed that
     * produces. What is left here is the jump and the reactions the owner's
     * horse made, replayed through the copy's own runtime and rider sync,
     * and drift its input cannot explain, which is made up in the copy's
     * own speed and heading -- or, past kSnapDistance (a saddle not yet
     * filled on this machine, a warp), corrected by placing the copy where
     * the owner's horse is, as an on-foot body is snapped once it falls too
     * far behind.
     */
    void Horse::PresentRemoteState() {
        bool held                                           = false;
        const HorsePoseSnapshot sampled                     = SampleRenderPose(&held);
        const KCDC::Common::Replication::PoseSnapshot &pose = sampled.body;

        // The frame's length, for the rates below; a gap longer than a
        // frame is a stall, not a turn to measure.
        const MafiaNet::Time now = MafiaNet::GetTime();
        const float dtSeconds    = _presentedAt != 0 && now > _presentedAt && now - _presentedAt <= kMaxFrameMs ? static_cast<float>(now - _presentedAt) / 1000.0F : 0.0F;
        _presentedAt             = now;

        // A jump the owner's horse made since this copy's rider sat down goes
        // in as the rider's own jump bit, as the owner's jump key put it in:
        // PreUpdate turns it into the request flag 0x80000, the rider input
        // latches it (OnMovementInput), and the runtime holds it until
        // ApplyPendingJumpRequest puts it into the horse's movement request.
        if (!IsRiddenByRemotePlayer()) {
            StopFollowingRider();
        }
        else if (!_followingRider) {
            _followingRider          = true;
            _appliedJumpSequence     = sampled.jumpSequence;
            _appliedReactionSequence = sampled.reactionSequence;
            _jumpsOwed               = 0;
        }
        else if (sampled.jumpSequence != _appliedJumpSequence) {
            // Owed rather than played at once: drawn a delay behind, the copy
            // can still be in the air from the jump before when the owner's
            // horse takes the next one off its landing, and a request made
            // then is lost. At most kMaxJumpsOwed, so a stalled stream does
            // not come back as a run of hops.
            _jumpsOwed           = static_cast<std::uint8_t>(std::min<int>(kMaxJumpsOwed, _jumpsOwed + static_cast<std::uint8_t>(sampled.jumpSequence - _appliedJumpSequence)));
            _appliedJumpSequence = sampled.jumpSequence;
        }

        // An owed jump goes in once the copy is on the ground and the one
        // before has been committed, straight into the runtime's pending
        // flag, where the rider's own key lands a frame later; the horse's
        // next update commits it (ApplyPendingJumpRequest, which the hook
        // lets through for a copy whatever its own speed or footing say --
        // the owner's horse already passed those checks).
        SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime = _horse->GetHorseRuntimeData();
        if (_followingRider && _jumpsOwed > 0 && runtime != nullptr && !_horse->IsJumping() && !runtime->IsJumpRequested()) {
            runtime->RequestJump();
            --_jumpsOwed;
        }

        // A quick start, a pull-up, a stop, a rear or a throw the owner's
        // horse made goes in as the horse's own reaction updates make it:
        // the horse's reaction action, its clip, first, then the ride state
        // through the copy's rider sync, so the copy's ride plays it and,
        // after a throw, tears itself down as the owner's did. Counted with
        // the pose, as the jumps are, so each plays where the copy stands
        // when its buffered pose reaches it, the rider's fall on the same
        // clock. One a frame, oldest first: the pose names the last
        // kReactionHistory, and a stop, a rear and a throw that fell between
        // two packets play in the order the owner's horse made them rather
        // than as the throw alone.
        if (_followingRider && sampled.reactionSequence != _appliedReactionSequence) {
            const std::uint8_t pending                         = std::min<std::uint8_t>(static_cast<std::uint8_t>(sampled.reactionSequence - _appliedReactionSequence), KCDC::Shared::Entities::HorseEntity::kReactionHistory);
            const std::uint8_t rideState                       = static_cast<std::uint8_t>((sampled.reactions >> (4 * (pending - 1))) & 0xF);
            _appliedReactionSequence                           = static_cast<std::uint8_t>(sampled.reactionSequence - pending + 1);
            SDK::wh::entitymodule::I_HorseRiderSync *riderSync = _horse->GetRiderSync();
            if (riderSync != nullptr && runtime != nullptr) {
                // The rider is a remote player, and the ride state it enters
                // has the rider's action say the local player's line about
                // it; the scope keeps that, and the rest of what the native
                // does for this machine's own player, off.
                const KCDC::Features::Player::NativePlayerModule::RemoteScope scope(true);

                // A quick start, a pull-up or a stop moves the ride off where it
                // is, which cancels a mount still playing on this machine (the
                // copy's mount ends after the owner's); the native asks whether
                // the rider holds the reins first, and so does this.
                if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_4 && riderSync->IsRiderInControl()) {
                    runtime->StartReactionAction(1);
                    riderSync->RequestQuickStart();
                }
                else if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_5 && riderSync->IsRiderInControl()) {
                    runtime->StartReactionAction(2);
                    riderSync->RequestRideState5();
                }
                else if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_6 && riderSync->IsRiderInControl()) {
                    runtime->StartReactionAction(5);
                    riderSync->RequestRideState6();
                }
                else if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_7 || rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_8) {
                    // The rear or the throw is the horse's own action, which
                    // the throw-off update starts before it asks the rider sync
                    // for the state; asked for the state alone the rider was
                    // thrown off a horse that never stood up.
                    runtime->StartReactionAction(rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_7 ? 3 : 4);
                    riderSync->RequestRideState7To9(static_cast<std::uint8_t>(rideState - SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_7));
                }
            }
        }

        if (!KCDC::Common::IsFinite(pose.position)) {
            return;
        }
        const glm::vec3 current = KCDC::Common::ToGlm(_entity->GetWorldPos());
        const float gap         = glm::length(glm::vec2(pose.position) - glm::vec2(current));
        if (gap > kSnapDistance) {
            Framework::Logging::GetLogger(kLogger)->debug("Horse {} copy drifted {:.1f} m from its owner's; placed", GetNetworkID(), gap);
            KCDC::Common::PlaceEntity(_entity, glm::vec3(pose.position.x, pose.position.y, GroundedHeight(pose.position)), pose.rotation);
            return;
        }

        // Ridden or not, the copy is kept on its owner's horse through its
        // own movement request, which the runtime builds each frame from its
        // speed (`BuildAndSubmitMovementRequest`, 0x1806CE7D4: the pace and
        // the delta move, `ApplyRuntimePaceAndDelta`) and which turns the
        // body toward its base rotation. Not in the air: a jump's arc is the
        // horse's own.
        if (runtime == nullptr || _horse->IsJumping() || !KCDC::Common::IsFinite(pose.velocity) || !KCDC::Common::IsFinite(pose.rotation)) {
            return;
        }

        // A pose held past the newest packet says nothing about where the
        // owner's horse is going. Seeded zero, the copy stopped dead and
        // restarted its gait from idle at full speed on the next packet.
        // The copy is told to stand and left to its own runtime: the
        // command's deceleration brings the speed down at the game's rate,
        // and the pace follows that speed, as it would under a rider who
        // let go of the reins. The facing holds.
        if (held) {
            if (_forcedMount != nullptr) {
                KCDC::Features::Player::NativePlayerModule::ClearMountLocomotion(_forcedMount);
            }
            if (_followingRider) {
                KCDC::Features::Player::NativePlayerModule::SetMountGait(_horse, -1);
            }
            _steerBend = 0.0F;
            _steerAt   = 0;
            return;
        }

        // The owner's facing, and the gap measured along and across it.
        const float ownerSpeed = glm::length(glm::vec2(pose.velocity));
        const bool ownerHalted = ownerSpeed < kMinLerpSpeed;
        float yaw              = KCDC::Common::YawOf(glm::normalize(pose.rotation));
        const glm::vec2 error  = glm::vec2(pose.position) - glm::vec2(current);
        const float errorAlong = glm::dot(error, glm::vec2(-std::sin(yaw), std::cos(yaw)));
        const float errorLeft  = glm::dot(error, glm::vec2(-std::cos(yaw), -std::sin(yaw)));

        // The speed is the owner's horse's, from its own packet, with the
        // gap along its path made up in it. The runtime speed is what the
        // rider's stick builds through UpdateHorseMovementSpeed and what the
        // pace, the delta move and the locomotion tags are read off, so a
        // copy behind is asked to go faster and one ahead slower, in its own
        // stride, instead of being pushed against it. Seeded every frame,
        // the stick's acceleration starts from there rather than from
        // wherever a late rider packet left it, and a riderless copy does not
        // run down by its own deceleration while a throw pulls the owner's
        // animal up hard. A halted copy is left where it stands unless it
        // parted from its owner's, and never backs up.
        //
        // But a riderless copy of a standing horse walks onto the spot that
        // horse stands on, once per stop, as a navigating copy settles
        // (`Settle`): its heading turned toward the spot, and its speed what
        // closes the gap, as far as it already faces it. Left to the rule
        // above it stood wherever it was beside or behind the spot, as far as
        // kSnapDistance: a copy that got up where its own ragdoll had put it
        // stood two or three metres off for good, and the election, which
        // measures from each simulator's own horse, handed the animal back
        // and forth between the two machines. Riderless on the wire as well
        // as here: a rider's get-on pairs a slave action on the horse, which
        // stands for it, and the seat here fills only once it lands. A rider
        // ends the stop as moving does.
        const bool riderless = riderNetworkId == 0 && !_followingRider;
        if (!ownerHalted || !riderless) {
            ResetSettle();
        }
        //
        // Either way the speed is held inside what the gait can show
        // (`ClampHorseFollowSpeed`): the owner's gait, which its speed picks
        // as the game picks any horse's, or a walk onto the spot the owner
        // stands on. What the clamp leaves of the gap is closed over the
        // frames after, or by the placement past kSnapDistance.
        const float ownerYaw                         = yaw;
        const bool settling                          = ownerHalted && riderless && Settle(gap);
        const KCDC::Shared::Game::HorseGaitBand band = KCDC::Shared::Game::HorseGaitBandFor(ownerHalted ? 0 : KCDC::Shared::Game::HorseMoveStyleForSpeed(ownerSpeed));
        float speed                                  = ownerSpeed;
        if (settling) {
            const glm::vec2 toward  = error / gap;
            const glm::vec3 forward = KCDC::Common::ToGlm(_entity->GetWorldRotation()) * glm::vec3(0.0F, 1.0F, 0.0F);
            const float facing      = glm::dot(toward, glm::normalize(glm::vec2(forward)));
            yaw                     = std::atan2(-toward.x, toward.y);
            speed                   = facing >= kSettleFacing ? KCDC::Shared::Game::ClampHorseFollowSpeed(ownerSpeed, true, std::min(kSettleSpeed, gap / kFollowSeconds) * facing, band) : 0.0F;
        }
        else if (!ownerHalted || errorAlong > kHaltedResyncGap) {
            const float allowed = std::max(kFollowSpeedDiff, ownerSpeed * kFollowSpeedShare);
            speed               = KCDC::Shared::Game::ClampHorseFollowSpeed(ownerSpeed, ownerHalted, ownerSpeed + std::clamp(errorAlong / kFollowSeconds, -allowed, allowed), band);
        }

        // The seed goes in now, for the frame that follows, and is held by
        // the hooks on the runtime's own passes: `UpdateHorseMovementSpeed`
        // puts the speed back after its acceleration toward the commanded
        // gait, and `ApplyRuntimePaceAndDelta` puts the owner's pace on the
        // request in place of the one it derived from the speed. The pace
        // is the owner's own, sampled with the pose: what its horse played
        // at this speed, hysteresis and all, so the copy plays the same
        // stride at the same moment. A copy walking where its owner stands
        // -- settling onto the spot, or closing more than kHaltedResyncGap
        // -- walks; a copy seeded no speed stands.
        float forcedPace = static_cast<float>(sampled.pace);
        if (speed <= 0.0F) {
            forcedPace = 0.0F;
        }
        else if (forcedPace <= 0.0F) {
            forcedPace = 1.0F;
        }
        runtime->SetSpeed(speed);
        KCDC::Features::Player::NativePlayerModule::SetMountLocomotion(_horse, forcedPace, speed);
        _forcedMount = _horse;

        // The heading turns toward the owner's at a bounded rate rather than
        // being written whole: a horse turns by delta alone -- the rider's
        // stick becomes an AddDeltaRotation (`ApplyRiderTurnToRequest`,
        // 0x18059B800), the frame's delta angles go into the base
        // (C_ActorRotation::Update), and `ProcessTurning` (0x1809D1D8C)
        // requests the whole difference between the entity and its base
        // every ground frame. Written whole, the base was a pivot the clip
        // did not animate. So the target -- the owner's facing, bent toward
        // the owner's line by the angle the sideways gap subtends over the
        // path ahead, so the copy steers back onto it rather than being slid
        // across -- is approached from where the body faces by no more than
        // the owner's own turn plus the gait's turn axis a second
        // (`StepHorseHeading`), and written into the base through the
        // horse's own setter (0x181ECC550), without the rider's angles. The
        // bend itself moves no faster than the turn axis leaves over once
        // the owner's own turn is played (`LimitHorseSteerBend`).
        float ownerTurnRate = 0.0F;
        if (!ownerHalted) {
            const float steerDt    = _steerAt != 0 && now > _steerAt && now - _steerAt <= kSteerMaxFrameMs ? static_cast<float>(now - _steerAt) / 1000.0F : 0.0F;
            ownerTurnRate          = steerDt > 0.0F ? KCDC::Common::WrapPi(ownerYaw - _steerOwnerYaw) / steerDt : 0.0F;
            const float lookAhead  = std::max(kSteerLookAheadMin, ownerSpeed * kSteerLookAheadSeconds);
            const float wantedBend = std::clamp(std::atan2(errorLeft, lookAhead), -kSteerMaxRadians, kSteerMaxRadians);
            _steerBend             = KCDC::Shared::Game::LimitHorseSteerBend(_steerBend, wantedBend, ownerTurnRate, steerDt, band);
            _steerOwnerYaw         = ownerYaw;
            _steerAt               = now;
            yaw += _steerBend;
        }
        else {
            _steerBend = 0.0F;
            _steerAt   = 0;
        }
        const float currentYaw = KCDC::Common::YawOf(glm::normalize(KCDC::Common::ToGlm(_entity->GetWorldRotation())));
        const float turnRate   = ownerHalted ? kStandTurnRate : std::fabs(ownerTurnRate) + band.turnSpeedMax;
        const float steppedYaw = KCDC::Shared::Game::StepHorseHeading(currentYaw, yaw, turnRate, dtSeconds, kHeadingSnapRadians);
        _horse->SetBaseRotationWithFlags(KCDC::Common::ToNative(glm::angleAxis(steppedYaw, glm::vec3(0.0F, 0.0F, 1.0F))), SDK::wh::entitymodule::C_Horse::kBaseRotationWithoutRider);

        // And the stride with it, under a rider: the gait the owner
        // commanded, sampled from the same packet, stated in place of the one
        // the rider copy's keys build (SubmitHorseMoveCommand hook). Those
        // keys travel in the rider's own stream, and in the frames between
        // the two a copy at the new speed in the old stride slides.
        if (_followingRider) {
            KCDC::Features::Player::NativePlayerModule::SetMountGait(_horse, static_cast<std::int32_t>(sampled.gaitCommand) - 1);
        }
    }

    void Horse::StopFollowingRider() {
        _followingRider = false;
        ReleaseMountIfIdle();
    }

    void Horse::StopForcingLocomotion() {
        if (_forcedMount != nullptr) {
            KCDC::Features::Player::NativePlayerModule::ClearMountLocomotion(_forcedMount);
        }
        _presentedAt = 0;
        ReleaseMountIfIdle();
    }

    void Horse::ReleaseMountIfIdle() {
        if (_followingRider) {
            return;
        }
        // By the key it was enrolled under, which outlives the binding: the
        // body may already be gone, and its address is only a key here. A
        // register left behind would hold the next actor at that address
        // to a dead horse's pace.
        const SDK::wh::entitymodule::C_Actor *mount = _forcedMount != nullptr ? _forcedMount : _horse;
        if (mount != nullptr) {
            KCDC::Features::Player::NativePlayerModule::ForgetMount(mount);
        }
        _forcedMount = nullptr;
    }

    // --- Capture: this client is the rider --------------------------------

    /**
     * Reads the pose of a horse the local player is riding.
     *
     * Nothing is written onto the body: the rider's own game is simulating it,
     * and this only copies out what that simulation produced.
     */
    void Horse::CaptureLocalState() {
        // What the buffer holds belongs to a pose this client stopped
        // following when it took the reins.
        _snapshots.Clear();

        // A jump is counted when the horse leaves the ground under this
        // machine's player, so every step a copy sees is one the animal made.
        const bool jumping = _horse->IsJumping();
        if (jumping && !_wasJumping) {
            KCDC::Core::gApplication->GetHorseService().NotifyMountJumped(_horse);
        }
        _wasJumping = jumping;

        // A corpse's transform is its ragdoll's, not the animal's: the entity
        // follows the articulated body it fell as, rolled onto its side. The
        // fall is not replicated -- every copy plays its own from the pose the
        // horse died in, as CryEngine leaves a ragdoll out of the physics
        // aspect it serializes (`CActor::NetSerialize`) -- so that pose is what
        // stays on the wire, and what a body streaming in later stands up at
        // before it dies in turn. Spawned in the ragdoll's transform instead,
        // a horse starts its own death rolled into the ground and lands as a
        // collapsed lump.
        if (!alive) {
            _forcedPosePending = false;
            return;
        }

        // A scripted move of a ridden horse arrives here as a warp, because
        // the rider is the only peer that can put the animal anywhere.
        if (_forcedPosePending) {
            _forcedPosePending = false;
            if (KCDC::Common::IsFinite(position)) {
                KCDC::Common::PlaceEntity(_entity, position, rotation);

                velocity = glm::vec3(0.0F);
            }
            return;
        }

        const glm::vec3 captured = KCDC::Common::ToGlm(_entity->GetWorldPos());
        if (!KCDC::Common::IsFinite(captured)) {
            return;
        }

        // The velocity this animal's own animation was driven by, which is
        // the movement request's and not a derivative of where it has been:
        // what a receiver's pose buffer extrapolates along.
        velocity = KCDC::Common::ToGlm(_horse->GetMovementVelocity());
        if (!KCDC::Common::IsFinite(velocity)) {
            velocity = glm::vec3(0.0F);
        }
        position = captured;
        rotation = KCDC::Common::ToGlm(_entity->GetWorldRotation());
        // The gait the rider's command asks for, sampled with the pose: the
        // copy's stride is told by this, its speed by the velocity above.
        const SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime = _horse->GetHorseRuntimeData();
        gaitCommand                                                  = runtime != nullptr ? static_cast<std::uint8_t>(runtime->GetGaitStyleId() + 1) : 0;
        // And the pace the horse played this frame: the pseudo speed of its
        // last movement request (slot 134, which `UpdateMannequinTags`
        // resolves the MoveSpeed tag from), an integer 0..4 by construction.
        const float playedPace = _horse->GetAnimationMovementPace();
        pace                   = std::isfinite(playedPace) ? static_cast<std::uint8_t>(std::clamp<long>(std::lround(playedPace), 0, Shared::Entities::HorseEntity::kMaxPace)) : 0;
        CaptureJumps();
    }

    /**
     * Continues the wire's jump and reaction counts with the ones this
     * machine's player's horse makes under them (`HorseService`), and names
     * the newest reactions' ride states.
     *
     * Continued from whatever count the wire carried when this client took
     * the horse over, so every copy sees a step rather than a reset; and only
     * while this machine's player is the one in the saddle, since the counter
     * goes on with whatever horse that is.
     */
    void Horse::CaptureJumps() {
        const HorseService &horses        = KCDC::Core::gApplication->GetHorseService();
        const std::uint8_t mountJumps     = horses.LocalMountJumps();
        const std::uint8_t mountReactions = horses.LocalMountReactions();
        if (!_capturingJumps || !IsRiddenByLocalPlayer()) {
            _capturingJumps = true;
            _jumpBase       = static_cast<std::uint8_t>(jumpSequence - mountJumps);
            _reactionBase   = static_cast<std::uint8_t>(reactionSequence - mountReactions);
        }
        jumpSequence     = static_cast<std::uint8_t>(_jumpBase + mountJumps);
        reactionSequence = static_cast<std::uint8_t>(_reactionBase + mountReactions);
        reactions        = horses.LocalMountReactionHistory();
    }

    /**
     * The pose to draw this frame.
     *
     * Drawn from the two packets bracketing this moment rather than from the
     * newest: the raw pose steps at the send rate, and carrying the newest one
     * forward moves the animal at the jitter of the network. The wire pose is
     * all there is until a second packet lands.
     */
    HorsePoseSnapshot Horse::SampleRenderPose(bool *held) {
        HorsePoseSnapshot pose;
        bool poseHeld = false;
        if (!_snapshots.Sample(KCDC::Common::Replication::PoseRenderTime(_snapshots), pose, &poseHeld)) {
            pose.body.position    = position;
            pose.body.velocity    = velocity;
            pose.body.rotation    = rotation;
            pose.jumpSequence     = jumpSequence;
            pose.gaitCommand      = gaitCommand;
            pose.pace             = pace;
            pose.reactionSequence = reactionSequence;
            pose.reactions        = reactions;
        }

        // A pose held past the newest packet stands still: the owner has
        // stopped reporting, so nothing says the animal is still going.
        if (poseHeld) {
            KCDC::Common::Replication::HoldLocomotion(pose.body);
            pose.pace = 0;
        }
        if (held != nullptr) {
            *held = poseHeld;
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
    float Horse::GroundedHeight(const glm::vec3 &wanted) {
        if (!_grounded) {
            return wanted.z;
        }
        if (glm::distance(glm::vec2(wanted), glm::vec2(_groundedPosition)) > kGroundedRadius) {
            _grounded = false;
            return wanted.z;
        }
        return _groundedPosition.z;
    }

    // --- Riderless navigation ----------------------------------------------

    void Horse::PresentNavigation() {
        const HorsePoseSnapshot sampled             = SampleRenderPose();
        const KCDC::Common::Replication::PoseSnapshot &pose = sampled.body;
        if (!KCDC::Common::IsFinite(pose.position) || !KCDC::Common::IsFinite(pose.velocity)) {
            return;
        }

        // Placed once, when this copy starts following, and again whenever it
        // has fallen further behind than walking could make up. Everything
        // between is travel.
        const glm::vec3 current = KCDC::Common::ToGlm(_entity->GetWorldPos());
        const glm::vec2 offset  = glm::vec2(pose.position) - glm::vec2(current);
        const float gap         = glm::length(offset);
        const glm::vec2 travel(pose.velocity);
        const float speed    = glm::length(travel);
        const bool following = _controllerUse == ControllerUse::Driving;
        const bool standing  = speed < kStandSpeed;
        const bool settling  = following && gap <= kSnapDistance && standing && Settle(gap);

        if (!following || gap > kSnapDistance) {
            if (following) {
                Framework::Logging::GetLogger(kLogger)->debug("Horse {} copy fell {:.1f} m behind; placed", GetNetworkID(), gap);
            }
            KCDC::Common::PlaceEntity(_entity, glm::vec3(pose.position.x, pose.position.y, GroundedHeight(pose.position)), pose.rotation);
        }
        else if (!settling) {
            // The facing is the simulator's, stated every frame: the gait's blend
            // spaces turn the body back at their own rate, and a slower push
            // loses to them -- the body gallops sideways after a hard turn.
            // Not while settling: the horse walks where it faces, so a copy
            // held to the simulator's facing could only sidle at the spot.
            // Only a change is written: every write raises the entity's
            // transform events, and a standing simulator's facing holds still.
            const glm::quat facing = glm::normalize(pose.rotation);
            if (std::fabs(glm::dot(facing, KCDC::Common::ToGlm(_entity->GetWorldRotation()))) < kFacingUnchanged) {
                _entity->SetRotation(KCDC::Common::ToNative(facing));
            }
        }

        SDK::CMovementRequest request {};
        if (standing) {
            if (settling) {
                // Aimed past the spot, along the way to it: the controller
                // counts a target inside its arrival radius as reached and
                // never starts, and the settle clears the target itself once
                // the copy is close enough.
                const glm::vec2 target = glm::vec2(pose.position) + (offset / gap) * kSettleOvershoot;
                request.SetMoveTarget(KCDC::Common::ToNative(glm::vec3(target, pose.position.z)));
                request.SetDesiredSpeed(kSettleSpeed);
                request.SetPseudoSpeed(kSettlePace);
            }
            else {
                // Standing, stated the way the horse's own runtime states it:
                // a pace of zero, rather than whatever pace the last frame of
                // the gait left in the controller.
                request.ClearMoveTarget();
                request.ClearDesiredSpeed();
                request.SetPseudoSpeed(0.0F);
            }
        }
        else {
            // Moving again: the next stop gets its own settle.
            ResetSettle();

            // The target sits well ahead along the simulator's travel: the
            // controller brakes into anything nearer, and at a gallop its
            // braking distance reaches eight metres. Aimed from where the simulator
            // was, so the steering also takes up any sideways drift.
            const glm::vec2 heading = travel / speed;
            const float lead        = std::clamp(speed * kLeadSeconds, kLeadMin, kLeadMax);
            const glm::vec2 target  = glm::vec2(pose.position) + heading * lead;

            // How far behind the simulator this copy is, along the way it is going,
            // leans the speed a little to close the gap -- a little, because
            // the gait's own motion carries the body, and asking far more of
            // it than the simulator's speed outruns the pose.
            const float lag     = glm::dot(glm::vec2(pose.position) - glm::vec2(current), heading);
            const float catchUp = std::clamp(1.0F + lag / lead, 1.0F / kCatchUpMax, kCatchUpMax);

            request.SetMoveTarget(KCDC::Common::ToNative(glm::vec3(target, pose.position.z)));
            request.SetDesiredSpeed(speed * catchUp);
            request.SetPseudoSpeed(Shared::Game::NpcSpeedPseudoOrdinal(navigationIntent.speed));
        }

        KCDC::Core::gApplication->GetHorseService().Drive(_horse, request);
        _controllerUse = ControllerUse::Driving;
    }

    /** Drives a server navigation step on the elected simulator. */
    void Horse::Navigate() {
        const bool canNavigate = navigationRevision != 0 && alive && riderNetworkId == 0 && _horse != nullptr && !_horse->HasRider() && _horse->IsSettled() && _broughtUp;
        if (!canNavigate) {
            ReleaseController();
            _navigationRevision = 0;
            _navigationTick     = 0;
            return;
        }
        const auto now     = MafiaNet::GetTime();
        const bool changed = _navigationRevision != navigationRevision || _controllerUse != ControllerUse::Navigating || _forcedPosePending;
        if (changed) {
            _navigationProgress.Reset();
            _navigationRevision = navigationRevision;
            _navigationFacing   = glm::normalize(KCDC::Common::ToGlm(_entity->GetWorldRotation()));
        }
        const float dt     = changed || _navigationTick == 0 ? 0.0F : std::min(static_cast<float>(now - _navigationTick) / 1000.0F, 0.25F);
        _navigationTick    = now;
        const auto current = KCDC::Common::ToGlm(_entity->GetWorldPos());
        auto status        = Shared::Game::NpcIntentStatus::Idle;
        SDK::CMovementRequest request {};
        request.ClearMoveTarget();
        request.ClearBodyTarget();
        request.ClearLookTarget();
        request.SetDesiredSpeed(0.0F);
        request.SetPseudoSpeed(0.0F);
        request.SetDistanceToPathEnd(0.0F);
        if (!navigationIntent.IsSane()) {
            status = Shared::Game::NpcIntentStatus::Failed;
        }
        else if (navigationIntent.kind == Shared::Game::NpcIntentKind::MoveTo) {
            if (navigationIntent.HasArrived(current, navigationIntent.point)) {
                status = Shared::Game::NpcIntentStatus::Reached;
            }
            else if (glm::distance(glm::vec2(current), glm::vec2(navigationIntent.point)) < 0.001F) {
                status = Shared::Game::NpcIntentStatus::Blocked;
            }
            else if (_navigationProgress.Update(current, dt)) {
                status = Shared::Game::NpcIntentStatus::Blocked;
            }
            else {
                status = Shared::Game::NpcIntentStatus::Running;
                request.SetMoveTarget(KCDC::Common::ToNative(navigationIntent.point));
                request.SetBodyTarget(KCDC::Common::ToNative(navigationIntent.point));
                request.SetLookTarget(KCDC::Common::ToNative(navigationIntent.point + glm::vec3(0.0F, 0.0F, 1.5F)));
                const glm::vec2 heading = glm::normalize(glm::vec2(navigationIntent.point - current));
                const glm::quat wanted  = glm::angleAxis(std::atan2(-heading.x, heading.y), glm::vec3(0.0F, 0.0F, 1.0F));
                const glm::quat facing  = _navigationFacing;
                const float remaining   = 2.0F * std::acos(std::clamp(std::fabs(glm::dot(facing, wanted)), 0.0F, 1.0F));
                // Native horse gait blends do not reliably turn a riderless
                // body onto its move target. Turn at 120 degrees per second,
                // and wait to walk until the destination is ahead of it.
                constexpr float turnRate = glm::radians(120.0F);
                const glm::quat turned   = remaining > 0.0001F ? glm::normalize(glm::slerp(facing, wanted, std::min(1.0F, turnRate * dt / remaining))) : wanted;
                _navigationFacing        = turned;
                if (std::fabs(glm::dot(turned, KCDC::Common::ToGlm(_entity->GetWorldRotation()))) < kFacingUnchanged) {
                    _entity->SetRotation(KCDC::Common::ToNative(turned));
                }
                const glm::vec3 forward = turned * glm::vec3(0.0F, 1.0F, 0.0F);
                const float alignment   = std::max(0.0F, glm::dot(heading, glm::vec2(forward)));
                request.SetDesiredSpeed(Shared::Game::NpcSpeedMetresPerSecond(navigationIntent.speed) * alignment);
                request.SetPseudoSpeed(Shared::Game::NpcSpeedPseudoOrdinal(navigationIntent.speed));
                request.SetDistanceToPathEnd(glm::distance(glm::vec2(current), glm::vec2(navigationIntent.point)) + navigationIntent.pathBeyond);
            }
        }
        else if (navigationIntent.kind != Shared::Game::NpcIntentKind::Hold) {
            status = Shared::Game::NpcIntentStatus::Failed;
        }
        // The same ownership gate used by observer horses suppresses native
        // riderless requests while this server order drives the controller.
        KCDC::Core::gApplication->GetHorseService().Drive(_horse, request);
        _controllerUse           = ControllerUse::Navigating;
        navigationStatus         = status;
        navigationStatusRevision = navigationRevision;
    }

    void Horse::ResetSettle() {
        _settleOver    = false;
        _settleCheckAt = 0;
    }

    /**
     * Whether a standing copy, `gap` metres from where its simulator's
     * horse stopped, should still be walking onto that spot.
     *
     * One attempt per stop. It ends for good -- until that horse moves again
     * -- once the copy is within reach, or once a whole window passes without
     * the gap shrinking.
     */
    bool Horse::Settle(float gap) {
        if (_settleOver) {
            return false;
        }
        if (gap <= kSettleDistance) {
            _settleOver = true;
            return false;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_settleCheckAt == 0) {
            _settleBestGap = gap;
            _settleCheckAt = now + kSettleWindowMs;
        }
        else if (now >= _settleCheckAt) {
            if (gap > _settleBestGap - kSettleProgress) {
                _settleOver = true;
                Framework::Logging::GetLogger(kLogger)->debug("Horse {} copy cannot close the last {:.2f} m; standing", GetNetworkID(), gap);
                return false;
            }
            _settleBestGap = gap;
            _settleCheckAt = now + kSettleWindowMs;
        }
        return true;
    }

    void Horse::ReleaseController() {
        if (_controllerUse == ControllerUse::None) {
            return;
        }

        // Let go first: the clear below is one of the requests the gate would
        // refuse while this copy is still enrolled as driven.
        KCDC::Core::gApplication->GetHorseService().ForgetHorse(_entityId);

        // A copy's steering is taken back out of the controller, or it keeps
        // walking the animal to the last place asked -- under a local rider
        // who has just taken the reins, or as a corpse.
        if (SDK::IMovementController *movement = _horse != nullptr ? _horse->GetMovementController() : nullptr) {
            KCDC::Common::ClearSteering(movement);
        }
        ResetSettle();
        _controllerUse = ControllerUse::None;
    }

    // --- The soul ---------------------------------------------------------

    std::string Horse::LocalizationKey() {
        // One word: the native UI '@'-prefixes each word separately, so a key
        // containing a space would resolve as two keys and fail both.
        return "kcdc_horse_" + std::to_string(GetNetworkID());
    }

    void Horse::ReconcileSoul() {
        auto *soul = _horse != nullptr ? _horse->GetSoul() : nullptr;
        if (soul == nullptr) {
            return;
        }

        ReconcileName(soul);
        ReportCapacity(soul);
        ReconcilePossession(soul);
    }

    void Horse::ReconcilePossession(SDK::wh::rpgmodule::C_Soul *soul) {
        SDK::wh::rpgmodule::C_Soul *master = KCDC::Features::Companion::ResolvePlayerSoul(ownerNetworkId);
        switch (KCDC::Features::Companion::ReconcileMaster(soul, master)) {
        case KCDC::Features::Companion::PossessionChange::Possessed: Framework::Logging::GetLogger(kLogger)->info("Horse {} possessed by player {}", GetNetworkID(), ownerNetworkId); break;
        case KCDC::Features::Companion::PossessionChange::Released: Framework::Logging::GetLogger(kLogger)->info("Horse {} released from its previous owner", GetNetworkID()); break;
        case KCDC::Features::Companion::PossessionChange::Refused: Framework::Logging::GetLogger(kLogger)->warn("Horse {} refused possession by player {}", GetNetworkID(), ownerNetworkId); break;
        case KCDC::Features::Companion::PossessionChange::None: break;
        }

        // A player's own horse cannot die: possessing it forwards "Player
        // Companion Immortality" to it from the master's "Player - companion
        // - horse", whose `C_CompanionPerk` (0x180964944) adds it to the
        // animal's perks (0x180A201C0 -> `AddPerk`, 0x180A20260) and takes it
        // off again on release (0x180A20348). A remote player's copy owns no
        // perks -- player progression answers `HasPerk` for it from the
        // replicated set instead -- so the forward happened on the owner's
        // machine alone, and a horse any other machine ran died there. It is
        // made here for whatever master this horse has: the horse owns the
        // one perk exactly while its master owns the other, as the game's
        // own forward already keeps them on the owner's machine.
        const bool forwarded = master != nullptr && soul->GetCompanionMaster() == master && master->HasPerk(KCDC::Common::ToCryGuid(kCompanionHorsePerk), false);
        if (forwarded != soul->HasPerk(KCDC::Common::ToCryGuid(kCompanionImmortalityPerk), false)) {
            SDK::wh::rpgmodule::C_PerkList *perks = soul->GetActiveStateStorage()->GetPerkList();
            const bool changed                    = forwarded ? perks->AddPerk(KCDC::Common::ToCryGuid(kCompanionImmortalityPerk), nullptr) : perks->RemovePerk(KCDC::Common::ToCryGuid(kCompanionImmortalityPerk));
            if (changed) {
                Framework::Logging::GetLogger(kLogger)->info("Horse {} {} its owner's companion immortality", GetNetworkID(), forwarded ? "took" : "lost");
            }
        }
    }

    /**
     * States the name key on the soul, if the server has renamed this horse.
     *
     * The key, not the name: the engine treats this field as one and the
     * localization hook turns it back. Compared against the soul rather than
     * latched, so a rebuilt soul is simply told again. An empty wire name
     * means nobody has renamed this horse, so its own name stands.
     */
    void Horse::ReconcileName(SDK::wh::rpgmodule::C_Soul *soul) {
        if (name.empty()) {
            return;
        }

        const std::string key = LocalizationKey();
        const char *current   = soul->GetUIName();
        if (current != nullptr && key == current) {
            return;
        }

        soul->SetUIName(key.c_str());
        Framework::Logging::GetLogger(kLogger)->info("Horse {} answers to '{}' as '{}'", GetNetworkID(), key, name);
    }

    /**
     * Tells the server what this animal can carry, once per answer.
     *
     * Not a LocalEvents report: this is not the local player doing something,
     * it is what this peer's engine says about an entity the module owns, read
     * from the mod's own tick rather than from a detour. A value the report is
     * already in flight for is not sent again while it makes the round trip.
     */
    void Horse::ReportCapacity(SDK::wh::rpgmodule::C_Soul *soul) {
        // Only the copy that runs the horse: a copy that has just streamed in
        // reads the bare animal's capacity until it has been dressed, and its
        // word used to overwrite the saddle's on the server.
        if (!IsOwner()) {
            return;
        }

        const float capacity = soul->GetDerivedStatValue(SDK::wh::rpgmodule::E_SoulDerivedStat::InventoryCapacity);
        if (!std::isfinite(capacity) || SameCapacity(capacity, inventoryCapacity) || SameCapacity(capacity, _reportedCapacity)) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        Shared::RPC::HorseStats stats;
        stats.horseNetworkId    = GetNetworkID();
        stats.inventoryCapacity = capacity;
        net->BroadcastRPC(stats);

        _reportedCapacity = capacity;
        Framework::Logging::GetLogger(kLogger)->info("Horse {} carries {:.2f}", GetNetworkID(), capacity);
    }

    // --- Gear -------------------------------------------------------------

    void Horse::ReconcileGear() {
        if (_horse == nullptr || !_broughtUp || _horse->IsPutAway()) {
            return;
        }

        std::string worn;
        if (!CaptureWornGear(worn)) {
            return;
        }

        // A server write this body has not been dressed in. Whatever this
        // copy wore before it, reported or not, is settled by it.
        if (gearRevision != _appliedGearRevision) {
            if (!ApplyServerGear()) {
                return;
            }
            _appliedGearRevision = gearRevision;
            _gearRepairs         = 0;
            _gearReported        = false;
            if (CaptureWornGear(worn)) {
                _observedGear = worn;
            }
            return;
        }

        if (worn == gear) {
            _observedGear = worn;
            return;
        }

        // Only the rider changes a horse's gear: the horse inventory screen is
        // the ridden horse's. A change on their copy since it last looked is
        // theirs, and goes to the server; the answer dresses every copy.
        if (IsRiddenByLocalPlayer() && worn != _observedGear) {
            _observedGear = worn;
            ReportGear(worn);
            return;
        }

        // Waiting on the answer to a report, for as long as one takes.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_gearReported && _reportedGearRevision == gearRevision && now < _gearReportedAt + kGearReportTimeoutMs) {
            return;
        }

        // Nobody here changed it, so a report went unanswered or the outfit
        // could not be made. Dressed again, a few times and not every frame:
        // gear it will not wear at all would otherwise be retried forever.
        if (_gearRepairs >= kGearRepairLimit || now < _nextGearRepair) {
            return;
        }
        ++_gearRepairs;
        _nextGearRepair = now + kGearRepairIntervalMs;
        Framework::Logging::GetLogger(kLogger)->info("Horse {} lost gear nobody here took off; dressing it again ({} of {})", GetNetworkID(), _gearRepairs, kGearRepairLimit);
        if (ApplyServerGear() && CaptureWornGear(worn)) {
            _observedGear = worn;
        }
    }

    void Horse::ReportGear(const std::string &worn) {
        if (_gearReported && worn == _reportedGear && _reportedGearRevision == gearRevision) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        Shared::RPC::HorseGearReport report;
        report.horseNetworkId  = GetNetworkID();
        report.gear            = worn;
        report.basedOnRevision = gearRevision;
        net->BroadcastRPC(report);

        _gearReported         = true;
        _reportedGear         = worn;
        _reportedGearRevision = gearRevision;
        _gearReportedAt       = MafiaNet::GetTime();
        Framework::Logging::GetLogger(kLogger)->info("Horse {} wears {} piece(s) of gear here; reported against revision {}", GetNetworkID(), worn.size() / Shared::Game::kPackedHorseGearWidth, gearRevision);
    }

    bool Horse::ApplyServerGear() {
        // Horse gear is armor the clothing system draws, so it is dressed the
        // way a person's clothes are: as the soul's own outfit, which the engine
        // then keeps on. No weapon listener: an animal has no shooting
        // expansion, and asking for one would build it.
        KCDC::Common::DressOptions options;
        options.weaponListener                        = false;
        const KCDC::Common::DressResult result = KCDC::Common::ApplyActorEquipment(_horse, gear, options);
        if (!result.ready) {
            return false;
        }
        if (result.changed) {
            Framework::Logging::GetLogger(kLogger)->info("Horse {} dressed in gear revision {}: {} piece(s)", GetNetworkID(), gearRevision, result.armorPieces);
        }
        return true;
    }

    void Horse::ForgetGear() {
        // The items belonged to the body that went; the next one is dressed
        // from the wire afresh.
        _appliedGearRevision = 0;
        _observedGear.clear();
        _gearReported = false;
        _gearRepairs  = 0;
    }

    // --- Buffs ------------------------------------------------------------

    void Horse::ReconcileBuffs() {
        if (_horse == nullptr || !_broughtUp || _horse->IsPutAway()) {
            return;
        }
        // A new list is applied at once; the one in force is checked again every second, so an
        // effect that ran out or that the game took off is put back while the server lists it.
        const auto now     = static_cast<std::uint64_t>(MafiaNet::GetTimeMS());
        const bool changed = buffRevision != _appliedBuffRevision;
        if (!changed && (now < _nextBuffCheck || (_appliedHorseBuffs.empty() && _ownedHorseBuffs.empty()))) {
            return;
        }
        auto *soul    = _horse->GetSoul();
        auto *manager = SDK::wh::rpgmodule::C_BuffManager::GetInstance();
        if (soul == nullptr || manager == nullptr) {
            return;
        }
        _nextBuffCheck = now + 1000;

        std::vector<Shared::RPC::BuffId> wanted = _appliedHorseBuffs;
        auto &customs                           = KCDC::Features::Buff::CustomBuffModule::Get();
        if (changed) {
            if (!Shared::Game::UnpackHorseBuffs(buffs, wanted)) {
                Framework::Logging::GetLogger(kLogger)->warn("Horse {} carries a buff list this build cannot read; applying the {} it could", GetNetworkID(), wanted.size());
            }

            // A custom definition the server registered but this client has not
            // been handed yet resolves to nothing. Wait for it rather than record
            // the revision as applied with the buff missing.
            for (const Shared::RPC::BuffId &buff : wanted) {
                const bool known  = Shared::Game::FindBuffById(buff) != nullptr || Shared::Game::CustomBuffRegistry::Get().Find(Shared::Game::CryGuidToString(buff.low, buff.high)) != nullptr;
                const auto native = customs.ToNative(buff);
                if (!known || (native.hipart == 0 && native.lopart == 0)) {
                    if (now >= _buffWaitLoggedAt + 5000) {
                        _buffWaitLoggedAt = now;
                        Framework::Logging::GetLogger(kLogger)->info("Horse {} waits for buff definition {} before applying revision {}", GetNetworkID(), Shared::Game::CryGuidToString(buff.low, buff.high), buffRevision);
                    }
                    return;
                }
            }
        }

        // Off first: the instances this copy put on that the server no longer lists, each by its own
        // handle. An instance that is gone -- it ran out, or the game took it off -- is no longer
        // this copy's, and neither is any instance of the same effect the game gave the horse
        // itself, the horseshoes' own say: removing by GUID would take those too.
        std::size_t removed = 0;
        for (auto owned = _ownedHorseBuffs.begin(); owned != _ownedHorseBuffs.end();) {
            if (manager->FindBuffInstance(owned->second) == nullptr) {
                owned = _ownedHorseBuffs.erase(owned);
                continue;
            }
            if (std::find(wanted.begin(), wanted.end(), owned->first) != wanted.end()) {
                ++owned;
                continue;
            }
            removed += manager->RemoveBuff(owned->second) ? 1 : 0;
            owned = _ownedHorseBuffs.erase(owned);
        }

        // Then on: whatever the soul does not already wear. An instance that
        // is there already -- this copy's, or because the game itself gave the
        // horse this effect -- is left alone rather than stacked.
        std::size_t added = 0;
        for (const Shared::RPC::BuffId &buff : wanted) {
            const SDK::CryGUID native = customs.ToNative(buff);
            bool present              = false;
            for (auto it = soul->ActiveBuffsBegin(); it != soul->ActiveBuffsEnd(); ++it) {
                const auto *definition = *it != nullptr ? (*it)->GetDefinition() : nullptr;
                if (definition != nullptr && definition->buffId.hipart == native.hipart && definition->buffId.lopart == native.lopart) {
                    present = true;
                    break;
                }
            }
            if (present) {
                continue;
            }
            // Null is not a failure: an implementation that folds the new
            // instance into an existing one returns null (see the player's
            // buff service). There is then no instance of this copy's to own,
            // and the next check finds the effect worn.
            if (const auto *instance = manager->AddBuffToSoul(soul, native, nullptr, SDK::CryGUID {}); instance != nullptr) {
                _ownedHorseBuffs.emplace_back(buff, instance->GetHandle());
                ++added;
            }
        }

        _appliedHorseBuffs   = std::move(wanted);
        _appliedBuffRevision = buffRevision;
        if (changed || added != 0 || removed != 0) {
            Framework::Logging::GetLogger(kLogger)->info("Horse {} wears buff revision {}: {} listed, {} put on, {} taken off", GetNetworkID(), buffRevision, _appliedHorseBuffs.size(), added, removed);
        }
    }

    void Horse::ForgetBuffs() {
        // They were on the soul that went with the body; the next body's soul
        // starts bare and is given the wire's list afresh.
        _appliedBuffRevision = 0;
        _appliedHorseBuffs.clear();
        _ownedHorseBuffs.clear();
        _nextBuffCheck = 0;
    }

    bool Horse::CaptureWornGear(std::string &out) {
        auto *soul    = _horse->GetSoul();
        auto *runtime = _horse->GetInventorySoulRuntime();
        if (soul == nullptr || runtime == nullptr || !runtime->IsInitialisedFor(soul)) {
            return false;
        }
        auto *manager = runtime->GetEquipmentManager();
        if (manager == nullptr) {
            return false;
        }

        // Walked in place: nothing the engine allocated crosses back out.
        Shared::Game::HorseGear worn;
        for (std::size_t i = 0; i < manager->GetEquippedCount(); ++i) {
            auto *item = manager->GetEquippedAt(i);
            if (item == nullptr) {
                continue;
            }
            const SDK::wh::entitymodule::S_ItemClassGUID itemClass = item->InvokeGetItemClassRaw();
            const Shared::Game::HorseGearEntry *entry              = Shared::Game::FindHorseGear(Shared::RPC::ItemClassId {itemClass.m_low, itemClass.m_high});
            if (entry != nullptr && worn.At(entry->slot) == nullptr) {
                worn.At(entry->slot) = entry;
            }
        }
        out = Shared::Game::PackHorseGear(worn);
        return true;
    }

    // --- Life -------------------------------------------------------------

    void Horse::OnLocalBlow(float amount, std::uint8_t reason) {
        KCDC::Core::gApplication->GetHorseService().QueueHit(GetNetworkID(), amount, reason);
    }

    void Horse::OnOwnDamage(std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) {
        KCDC::Core::gApplication->GetHorseService().QueueDamage(GetNetworkID(), attackerNetworkId, amount, reason);
        _lastAttackerNetworkId = attackerNetworkId;
        _lastBlowReason        = reason;
    }

    void Horse::OnSoulDied() {
        // Only the copy that decides turns a death into a report; a mirror's
        // death is the verdict it was told, already known everywhere.
        if (!IsOwner() || !alive) {
            return;
        }
        alive           = false;
        health          = 0.0F;
        killerNetworkId = _lastAttackerNetworkId;
        deathReason     = _lastBlowReason;
        KCDC::Core::gApplication->GetHorseService().QueueDeath(GetNetworkID(), killerNetworkId, deathReason);
        Framework::Logging::GetLogger(kLogger)->info("Horse {} died (killer {}, reason {})", GetNetworkID(), killerNetworkId, deathReason);
    }

    void Horse::ReconcileLife() {
        // Not before the body is up. A fresh horse is spawned put away and
        // woken by the put-away manager later, and until then it has no
        // update slot, no collider and a ragdoll manager that refuses to
        // physicalize -- no state to play a death in. In practice the wake
        // lands within a frame or two, and a horse killed that early was
        // shown in game to fall like any other; the collapsed corpses were
        // the spawn pose (see `CaptureLocalState`). This keeps the order
        // stated rather than relied on.
        auto *soul = _horse != nullptr ? _horse->GetSoul() : nullptr;
        if (soul == nullptr || !_broughtUp || _horse->IsPutAway()) {
            return;
        }

        // A new simulator starts from the wire, not from what its copy held
        // as an observer: that copy refused every health write it was sent.
        // So does its knockout, which the forced state hands it in
        // `unconscious`. A horse the wire says is up comes to here as well,
        // whatever knocked this copy out. One the wire says is out stays out
        // -- this copy went down with it, and its soul reports it so -- but
        // not in the copy's `unconscious_permanent`, which has no time of its
        // own: that is traded for the knockout a blow gives this horse
        // (kBlowKnockoutBuff), so it comes round by itself as it would have
        // where it was knocked out -- after a whole knockout from the
        // hand-over, since how much of the last simulator's had worn off is
        // not on the wire. Taken off first, and the new one put on in the
        // same call: the one that leaves stops the unconscious action
        // (`DeactivateOnActor`, 0x182BDB208) and the new one asks for it
        // again (`ActivateOnActor`, 0x181F9C590). Should the game refuse the
        // new one that action, it drops the buff (0x181F9C470), the horse is
        // up here and reports it so, and the copies come to with it.
        const bool simulating = IsOwner();
        if (simulating != _simulating) {
            _simulating  = simulating;
            _lifeApplied = false;
            if (simulating && !unconscious && (_knockedOut || soul->IsUnconscious())) {
                Wake();
            }
            else if (simulating && _knockedOut) {
                _knockedOut          = false;
                _nextKnockoutAttempt = 0;
                if (SDK::wh::rpgmodule::C_BuffManager::GetInstance()->RemoveAllBuffsByGuid(soul, KCDC::Common::ToCryGuid(kUnconsciousBuff)) > 0) {
                    SDK::wh::rpgmodule::C_UnconsciousBuffInitParams knockout(kImmortalKnockoutDepth, kBlowKnockoutCause);
                    SDK::wh::rpgmodule::C_BuffManager::GetInstance()->AddBuffToSoul(soul, KCDC::Common::ToCryGuid(kBlowKnockoutBuff), &knockout, SDK::CryGUID {});
                }
            }
        }

        if (!simulating) {
            PresentLife(soul);
            return;
        }

        if (!_lifeApplied || _appliedVitalsRevision != vitalsRevision) {
            _lifeApplied           = true;
            _appliedVitalsRevision = vitalsRevision;
            ApplyServerLife(soul);
        }
        CaptureLife(soul);
    }

    /**
     * Hands the server's life write to the copy that runs the horse, through
     * the soul's own setter, so the rest of the game reacts to it the way it
     * reacts to any other.
     */
    void Horse::ApplyServerLife(SDK::wh::rpgmodule::C_Soul *soul) {
        auto &life = KCDC::Common::Replication::BodyLife::Get();
        if (!alive) {
            if (!soul->IsDead()) {
                life.Kill(_horse);
            }
            return;
        }

        const float maximum = soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
        const float wanted  = health > 0.0F ? std::min(health, maximum) : maximum;
        if (!std::isfinite(wanted) || wanted <= 0.0F) {
            return;
        }

        if (soul->IsDead()) {
            life.Revive(_horse, wanted);
            Framework::Logging::GetLogger(kLogger)->info("Horse {} revived", GetNetworkID());
            return;
        }

        if (std::fabs(soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health) - wanted) > kHealthEpsilon) {
            life.SetHealth(_horse, wanted);
        }
    }

    /**
     * Reports the numbers this copy's soul holds. The death is not read here:
     * it arrives as an edge, `OnSoulDied`, from the engine's own dispatch.
     */
    void Horse::CaptureLife(SDK::wh::rpgmodule::C_Soul *soul) {
        const float maximumHealth  = soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
        const float maximumStamina = soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Stamina);
        if (std::isfinite(maximumHealth) && std::isfinite(maximumStamina)) {
            maxHealth  = maximumHealth;
            maxStamina = maximumStamina;
        }
        ReportVital(stamina, soul->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina), maxStamina);
        if (alive) {
            ReportVital(health, soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health), maxHealth);
        }
        unconscious = soul->IsUnconscious();
    }

    /**
     * Plays the simulator's verdict on a copy that does not run the horse.
     *
     * The soul is what dies, not the body: a copy whose health every write
     * was refused still has a living soul, and `HandleDeath` played on it
     * alone showed nothing. Whether the copy is dead is its soul's to say;
     * a death verdict is stated to it once per revision, since a soul the
     * game holds immortal does not die of it. The knockout is a buff this
     * copy puts on until it takes, so that one is latched (`_knockedOut`).
     */
    void Horse::PresentLife(SDK::wh::rpgmodule::C_Soul *soul) {
        // First the knockout, which a death ends as well. The simulator's
        // horse is knocked out where the game would kill a soul it holds
        // immortal -- `C_Soul::SetState` (0x18046EA68) stops its health at
        // ImmortalHealthMin -- and a player's own horse is one. Whether it is
        // out is the simulator's soul's to say, so this copy wears the
        // game's own unconscious buff for as long as the simulator's horse is
        // out, and the buff does the rest as it did there: it takes the body
        // down (`ActivateOnActor`, 0x181F9C5A0) and keeps it down (the
        // ragdoll's stand-up waits on `IsUnconscious`, 0x180905830). Not with
        // a rider on it, as the death below.
        //
        // When the simulator's horse comes to, this copy is woken as the
        // simulator's horse is (`Wake`), not only stripped of the buff it
        // wore: its soul can hold the game's own `unconscious` as well,
        // which `unconscious_permanent` does not evict (both are
        // IdExclusive). A copy refuses that buff
        // (`HorseService::RefusesKnockout`), but a machine that ran the horse
        // when it was knocked out keeps the one it had once another one runs
        // it. Taking off this copy's buff alone left it lying after its
        // simulator's horse had been woken.
        //
        // Put on until it takes. The game drops the buff again when the
        // unconscious action it asks for is refused, or the soul is not
        // unconscious by the time it is asked (0x181F9C470 clears it, and the
        // buff's next update ends it, 0x182BDD7B8) -- a copy can still be
        // playing the reaction to the very blow -- and one put on once left
        // a copy standing while its simulator's horse lay. So while the
        // simulator's horse is out and this soul is not, the buff is put on
        // again, at most every kKnockoutRetryMs, as a refused carry grab is
        // asked again.
        // The stamina is the simulator's: the game caps the gait tier
        // (`ResolveMoveSpeedTag`, 0x1807FEB60) and the style's speed by the
        // soul's own stamina, and a copy that drained its own under a long
        // gallop showed a lower gait than its owner. Written through the
        // soul's setter, which clamps it, whenever this copy's has drifted
        // a whole point from what the simulator last reported.
        if (alive && !soul->IsDead() && maxStamina > 0.0F && std::isfinite(stamina) && std::fabs(soul->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina) - stamina) > kStaminaEpsilon) {
            soul->SetState(SDK::wh::rpgmodule::E_SoulState::Stamina, std::clamp(stamina, 0.0F, maxStamina));
        }

        const bool knockedOut    = alive && unconscious && GetRider() == nullptr;
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (knockedOut && !soul->IsUnconscious() && now >= _nextKnockoutAttempt) {
            _nextKnockoutAttempt = now + kKnockoutRetryMs;
            SDK::wh::rpgmodule::C_BuffManager::GetInstance()->AddBuffToSoul(soul, KCDC::Common::ToCryGuid(kUnconsciousBuff), nullptr, SDK::CryGUID {});
        }
        if (knockedOut != _knockedOut) {
            if (knockedOut) {
                _knockedOut = true;
            }
            else {
                Wake();
            }
            Framework::Logging::GetLogger(kLogger)->info("Horse {} {} on its simulator; so is this copy", GetNetworkID(), knockedOut ? "was knocked out" : "is no longer knocked out");
        }

        auto &life = KCDC::Common::Replication::BodyLife::Get();
        if (!alive && !soul->IsDead()) {
            // Not with a rider on it. The lethal blow is reacted to on the
            // spot and the death plays after it, while the game only takes
            // a rider off a living horse -- so this copy stays standing until
            // its saddle is empty, which the rider's own ride replication
            // brings about with the game's forced get-off (Human::ApplyRide),
            // as it did on the rider's machine.
            if (GetRider() != nullptr) {
                return;
            }

            // Once per verdict. A soul the game holds immortal is knocked
            // out by the blow rather than killed (`C_Soul::SetState`,
            // 0x18046EA68, keeps its health at ImmortalHealthMin); struck
            // for as long as its soul was not dead, such a copy was struck
            // every frame.
            if (_lifeApplied && _appliedVitalsRevision == vitalsRevision) {
                return;
            }
            _lifeApplied           = true;
            _appliedVitalsRevision = vitalsRevision;
            life.Kill(_horse);
            Framework::Logging::GetLogger(kLogger)->info("Horse {} died on its simulator; killing this copy", GetNetworkID());
            return;
        }

        if (alive && soul->IsDead()) {
            life.Revive(_horse, soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health));
            Framework::Logging::GetLogger(kLogger)->info("Horse {} revived", GetNetworkID());
        }
    }

    /**
     * The game's own early wake, the buff its AI gives an unconscious body
     * (`interrupt_attack.xml`). It is ClassExclusive in the knockout's class,
     * so attaching it evicts every knockout the soul holds
     * (`ResolveBuffExclusivity`, 0x18063B300) -- a blow's `unconscious` and
     * a copy's `unconscious_permanent` alike -- and each one's exit stands
     * the body up (`DeactivateOnActor`, 0x182BDB208). It carries no effect
     * of its own and expires at once, leaving health as it is.
     */
    void Horse::Wake() {
        _knockedOut          = false;
        _nextKnockoutAttempt = 0;
        SDK::wh::rpgmodule::C_BuffManager::GetInstance()->AddBuffToSoul(_horse->GetSoul(), KCDC::Common::ToCryGuid(kWakeBuff), nullptr, SDK::CryGUID {});
    }

    // --- Lifetime ---------------------------------------------------------

    /**
     * Grounds the wire position before the body is created.
     *
     * The server has no engine and places a horse at the caller's own Z, which
     * floats it wherever the ground under the two is not level.
     */
    glm::vec3 Horse::ResolveSpawnPlacement() {
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

    void Horse::RequestSpawn() {
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
            Framework::Logging::GetLogger(kLogger)->error("Cannot spawn horse {}: position is not finite", GetNetworkID());
            return;
        }

        KCDC::Common::Spawn::SpawnRequest request;
        request.actorClass = SDK::ActorClassNames::Horse;
        request.soulGuid   = soulGuid;
        request.world      = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(ResolveSpawnPlacement()), KCDC::Common::ToNative(rotation)};

        _spawnTicket = KCDC::Core::gApplication->GetActorSpawner().Request(request, [this](KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
            OnSpawned(ticket, spawned);
        });
    }

    void Horse::OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
        // The ticket has run either way, so it never fires again.
        _spawnTicket = nullptr;

        if (!spawned) {
            if (KCDC::Common::Spawn::IsRetryableSpawnResult(ticket.GetResult())) {
                // Reported once rather than once a frame: a level load would
                // otherwise cost a log line every tick it takes to finish.
                if (!_spawnDeferred) {
                    _spawnDeferred = true;
                    Framework::Logging::GetLogger(kLogger)->info("Holding the spawn of horse {}: {}", GetNetworkID(), KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
                }
                return;
            }

            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Could not spawn horse {} (soul '{}'): {}", GetNetworkID(), soulGuid, KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
            return;
        }

        _entityId = ticket.GetEntityId();
        RefreshNativeBinding();
        if (_horse == nullptr) {
            Despawn();
            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Rolled back horse {}: its entity registered no horse actor", GetNetworkID());
            return;
        }

        // Let the native manager schedule wakeup for the initialized horse.
        _horse->RequestPutAway(false);

        // A fresh body has a fresh soul: the life the wire carries is stated
        // to it again, a dead horse included.
        _lifeApplied         = false;
        _knockedOut          = false;
        _nextKnockoutAttempt = 0;
        _spawnDeferred       = false;
        _nextSpawnAttempt    = 0;

        Framework::Logging::GetLogger(kLogger)->info("Spawned horse {} (soul '{}') as entity {}", GetNetworkID(), soulGuid, _entityId);
    }

    void Horse::ReleaseSpawnTicket() {
        if (_spawnTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().Return(_spawnTicket);
        }
        _spawnTicket = nullptr;
    }

    void Horse::Despawn() {
        // Dropped first, so a body that was still queued is never made for a
        // horse that has already gone.
        ReleaseSpawnTicket();

        if (_entityId == 0) {
            return;
        }

        // Anyone in the saddle comes off first and without a get-off, which
        // reads the ground beside a horse that is about to stop existing --
        // and before the seat register forgets the saddle, which is what
        // tells AbandonRide there is anyone in it.
        if (auto *rider = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(riderNetworkId)) {
            rider->AbandonRide();
        }

        KCDC::Core::gApplication->GetHorseService().ForgetSeatsFor(_entityId);
        KCDC::Common::Replication::BodyLife::Get().Unbind(this);

        // Released before the body goes, or its owner keeps a companion
        // pointer into an animal that no longer exists. Re-read first: the
        // binding is a frame-scoped memo, and this can run outside the tick.
        RefreshNativeBinding();
        if (_horse != nullptr) {
            KCDC::Features::Companion::ReleaseFromMaster(_horse->GetSoul());
        }

        ReleaseController();
        StopFollowingRider();
        StopForcingLocomotion();
        if (_knockedOut && _horse != nullptr) {
            Wake();
        }
        KCDC::Common::Replication::BodyLife::PrepareForRemoval(_horse);
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().DespawnNow(_entityId);
            Framework::Logging::GetLogger(kLogger)->info("Despawned horse {} (entity {})", GetNetworkID(), _entityId);
        }
        _entityId    = 0;
        _entity      = nullptr;
        _horse       = nullptr;
        _broughtUp   = false;
        _lifeApplied = false;
        _snapshots.Clear();
        ForgetGear();
        ForgetBuffs();
    }
} // namespace KCDC::Features::Horse
