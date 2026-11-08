#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "features/horse/horse_service.h"
#include "features/player/native_player_module.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/wh/entitymodule/c_actor_movement_controller.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_rider_player_control.h"
#include "sdk/wh/entitymodule/c_rider_player_input.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace KCDC::Features::Horse {
    namespace {
        using RegisterInAISystem_t                        = bool(__fastcall *)(SDK::IEntity *, const void *);
        RegisterInAISystem_t g_registerInAISystemOriginal = nullptr;

        // What ActorSpawner names a replicated horse. The name travels in the
        // spawn parameters, so it is on the entity before its script reaches
        // BasicAI:RegisterAI.
        constexpr char kHorsePrefix[]         = "KCDC_Horse_";
        constexpr std::size_t kHorsePrefixLen = sizeof(kHorsePrefix) - 1;

        bool IsReplicatedHorse(SDK::IEntity *entity) {
            const char *name = entity != nullptr ? entity->GetName() : nullptr;
            return name != nullptr && std::strncmp(name, kHorsePrefix, kHorsePrefixLen) == 0;
        }

        // RegisterInAISystem reads only the type word at +0x3A0 and constructs
        // nothing that would need tearing down, so a zeroed frame this size is
        // a complete "register nothing".
        constexpr std::size_t kAIObjectParamsSize = 960;

        /**
         * Keeps a replicated horse from ever being given an AI.
         *
         * `Scripts/Entities/AI/Horse.lua` declares an AIMovementAbility and
         * inherits `BasicAI:RegisterAI`, so a horse comes up steering itself --
         * unlike the human copies, which spawn as `NPC_NAI` whose RegisterAI
         * is a documented no-op. There is no `Horse_NAI` to spawn instead, and
         * an animal that decides where to walk is a second writer of the
         * transform the network is placing.
         *
         * Refused at creation rather than removed afterwards, because only the
         * removal can fail: the engine keeps the AI object of an entity holding
         * the 0x200000 extended flag, but nothing stops it skipping one.
         *
         * Only the deciding goes. Mounting reaches the rider expansion and the
         * horse usage manager, riding the ride context and the rider sync;
         * none of them consult the AI object. The one thing riding does that
         * does is the ridden-idle navmesh snap, answered below.
         */
        bool __fastcall RegisterInAISystem_Hook(SDK::IEntity *entity, const void *params) {
            if (!IsReplicatedHorse(entity)) {
                return g_registerInAISystemOriginal(entity, params);
            }

            // The original rather than the entity's own virtual, which would
            // re-enter this hook.
            alignas(16) std::uint8_t none[kAIObjectParamsSize] {};
            return g_registerInAISystemOriginal(entity, none);
        }

        using SnapToNavMesh_t                   = void(__fastcall *)(SDK::wh::entitymodule::HorseRiddenUpkeepLayout *);
        SnapToNavMesh_t g_snapToNavMeshOriginal = nullptr;

        /**
         * Leaves a horse without an AI where it stands after it has idled
         * under a player rider.
         *
         * The snap finds the navmesh through the AI object's navigation agent
         * and dereferences it unchecked, so on a horse `RegisterInAISystem_Hook`
         * refused -- the owner's own replicated mount, ~15 s after it stopped
         * -- it faulted. An animal with no agent has no navmesh to be put back
         * on, which is the answer the snap gives when its lookup fails.
         */
        void __fastcall SnapToNavMesh_Hook(SDK::wh::entitymodule::HorseRiddenUpkeepLayout *upkeep) {
            if (upkeep->GetRuntimeData()->GetHorse()->GetEntity()->GetAI() == nullptr) {
                return;
            }
            g_snapToNavMeshOriginal(upkeep);
        }

        using TryPlaceHorseNearPlayer_t                             = bool(__fastcall *)(SDK::wh::entitymodule::C_RiderPlayerControl *, SDK::IEntity *, bool, bool);
        TryPlaceHorseNearPlayer_t g_tryPlaceHorseNearPlayerOriginal = nullptr;

        /**
         * Leaves a horse without an AI where it stands when it is whistled
         * for from too far away to walk back.
         *
         * `CallHorse`'s far branch does not ask the animal to come; it finds a
         * spot in the player's view cone and puts it there. Both of its
         * placement branches go through the horse's AI object, which it looks
         * up by entity id and dereferences without a null check, so on a horse
         * `RegisterInAISystem_Hook` refused -- every replicated one -- the
         * whistle faulted.
         *
         * Refused rather than placed by hand, because where a replicated horse
         * is is not this client's to say unless it owns the animal, and even
         * then the move belongs in the horse's own replication rather than in
         * a native teleport the other copies never hear about. The near branch
         * already does nothing on a replicated horse: it issues a Lua horse
         * command, which needs the brain the animal does not have.
         */
        bool __fastcall TryPlaceHorseNearPlayer_Hook(SDK::wh::entitymodule::C_RiderPlayerControl *control, SDK::IEntity *horse, bool useNavigationPlacement, bool placementOption) {
            if (horse->GetAI() == nullptr) {
                return false;
            }
            return g_tryPlaceHorseNearPlayerOriginal(control, horse, useNavigationPlacement, placementOption);
        }

        using SetPlayerHorse_t                    = bool(__fastcall *)(SDK::wh::entitymodule::C_RiderPlayerControl *, SDK::EntityId);
        SetPlayerHorse_t g_setPlayerHorseOriginal = nullptr;

        using ShouldThrowOffRider_t                         = bool(__fastcall *)(SDK::wh::entitymodule::C_Horse *);
        ShouldThrowOffRider_t g_shouldThrowOffRiderOriginal = nullptr;

        /**
         * Whether a scared horse throws its rider (0x180CF8524), asked from
         * `HorseRuntimeData::Update`, which a remote rider's copy now runs
         * natively. Throwing another player off is their machine's decision;
         * the dismount it causes replicates from there, as does the
         * "HorseScared" tutorial the same branch shows its rider.
         */
        bool __fastcall ShouldThrowOffRider_Hook(SDK::wh::entitymodule::C_Horse *horse) {
            if (KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                return false;
            }
            return g_shouldThrowOffRiderOriginal(horse);
        }

        /**
         * Keeps a replicated horse out of the singleplayer "this is my horse".
         *
         * Whose horse a replicated one is comes from the server, and each
         * client states it on its own souls (`Horse::ReconcilePossession`).
         * `SetPlayerHorse` would state a second answer on this client alone,
         * and it is destructive on the way: it strips the horse's armour and
         * moves the previous horse's whole inventory onto it. The game reaches
         * it without being asked, too -- `C_Player::HandleEvent` makes the
         * first horse a horseless player mounts theirs.
         *
         * Clearing the assignment (id 0) and any horse that is not replicated
         * go through untouched. Tested by name, like the AI hook above, since
         * `C_Player::ProcessEvent` reaches this too and a lookup must not cost
         * a walk of every replica.
         */
        bool __fastcall SetPlayerHorse_Hook(SDK::wh::entitymodule::C_RiderPlayerControl *control, SDK::EntityId horseId) {
            // A remote player's rider control states nothing here at all: whose
            // horse anything is on this machine is this machine's player's.
            if (KCDC::Features::Player::NativePlayerModule::IsRemote(control->GetPlayer())) {
                return false;
            }
            // Only a live C_Player reaches here, so the entity system is up.
            if (horseId != 0 && IsReplicatedHorse(SDK::IEntitySystem::GetInstance()->GetEntity(horseId))) {
                return false;
            }
            return g_setPlayerHorseOriginal(control, horseId);
        }

        using RequestMovement_t                     = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorMovementController *, SDK::CMovementRequest &);
        RequestMovement_t g_requestMovementOriginal = nullptr;

        // A riderless runtime submits idle requests every frame. While an
        // order drives it, only our request may reach the controller. A live
        // rider always bypasses the gate, even before the next replica tick.
        bool __fastcall RequestMovement_Hook(SDK::wh::entitymodule::C_ActorMovementController *controller, SDK::CMovementRequest &request) {
            auto *actor = controller != nullptr ? controller->GetActor() : nullptr;
            if (actor != nullptr && actor->GetActorClass() == SDK::wh::entitymodule::C_Horse::ActorClassId && KCDC::Core::gApplication != nullptr) {
                auto *horse = static_cast<SDK::wh::entitymodule::C_Horse *>(actor);
                if (!horse->HasRider() && !KCDC::Core::gApplication->GetHorseService().OnHorseMovementRequest(actor->GetEntityId(), request)) {
                    return true;
                }
            }
            // A remote player's body faces and aims as its owner does: the
            // owner's yaw is its base, and that yaw with the owner's look
            // pitch its view (Human::PresentRemoteState). Its fighter's combat
            // modifiers aim at its opponent on this machine, and while the
            // body stands its movement controller turns it toward either
            // target (C_ActorMovementController::Update):
            //  - LookEnemy looks Hard (0x1827C21A7), which the controller
            //    turns the lower body to unless the style is NoLower
            //    ("lookIdleTurn", 0x1804BA346);
            //  - Aim sets an aim target (0x180D566F1), which it turns to
            //    whatever the look ("aimIdleTurn", 0x1804BA61A).
            // The turn ran the copy round ahead of its owner in a guard whose
            // movement the animation drives, set its base off the owner's yaw
            // every frame, and as a turn-in-place of the full angle to the
            // target rocked it on every lock-on. So its head still looks,
            // without the lower body, and it takes no aim target from its
            // fighter: the controller then aims along the body's eye direction
            // (0x1804B8FB1), which its movement state takes from its view
            // (UpdateMovementState, 0x1806412AC). The aim a drawn bow,
            // crossbow or firearm takes from PlayerMissileService names a fire
            // target with it, which the Aim modifier never does, and stays.
            if (KCDC::Features::Player::NativePlayerModule::IsRemote(actor)) {
                if (request.HasLookStyle()) {
                    if (request.GetLookStyle() == SDK::CMovementRequest::LOOKSTYLE_HARD) {
                        request.SetLookStyle(SDK::CMovementRequest::LOOKSTYLE_HARD_NOLOWER);
                    }
                    else if (request.GetLookStyle() == SDK::CMovementRequest::LOOKSTYLE_SOFT) {
                        request.SetLookStyle(SDK::CMovementRequest::LOOKSTYLE_SOFT_NOLOWER);
                    }
                }
                if (request.HasAimTarget() && !request.HasFireTarget()) {
                    request.ClearAimTarget();
                }
            }
            return g_requestMovementOriginal(controller, request);
        }

        using SubmitHorseMoveCommand_t                            = char(__fastcall *)(std::uint8_t *, std::int32_t *, std::uint8_t *, std::uint32_t *, char);
        SubmitHorseMoveCommand_t g_submitHorseMoveCommandOriginal = nullptr;

        /**
         * The rider's command intake (0x180A5019C): the gait move style the
         * rider input built from its keys, the forward/back flags and the
         * backing request, written into the runtime every frame. On a copy
         * ridden by a remote player the gait is the owner's instead, sampled
         * with the horse's pose, so the stride changes in the same frame as
         * the speed seeded from that pose; the keys' own gait would arrive
         * in the rider's stream, a few frames apart, and the copy would run
         * the new speed in the old stride until it did. The flags and the
         * backing stay the keys': they say which way, not how fast.
         */
        char __fastcall SubmitHorseMoveCommand_Hook(std::uint8_t *speedState, std::int32_t *gaitStyleId, std::uint8_t *backward, std::uint32_t *flags, char latch) {
            auto *runtime                         = *reinterpret_cast<SDK::wh::entitymodule::HorseRuntimeDataLayout **>(speedState);
            SDK::wh::entitymodule::C_Horse *horse = runtime != nullptr ? runtime->GetHorse() : nullptr;
            std::int32_t ownerGait                = -1;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider()) && KCDC::Features::Player::NativePlayerModule::MountGait(horse, ownerGait)) {
                std::int32_t gait = ownerGait;
                return g_submitHorseMoveCommandOriginal(speedState, &gait, backward, flags, latch);
            }
            return g_submitHorseMoveCommandOriginal(speedState, gaitStyleId, backward, flags, latch);
        }

        using ApplyPendingJumpRequest_t                             = void(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *, std::uint64_t *);
        ApplyPendingJumpRequest_t g_applyPendingJumpRequestOriginal = nullptr;

        /**
         * Where a horse's jump becomes a fact: the rider's jump request
         * (`ApplyHorseMovementCommands`) waits in the runtime until this puts
         * it into the movement request -- once the animal is mounted, fast
         * enough and free to jump -- and clears it. A request that was there
         * and is gone is a jump; the wire counts the ones under this
         * machine's player, and every copy replays each through its rider.
         */
        void __fastcall ApplyPendingJumpRequest_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, std::uint64_t *requestFlags) {
            const bool pending = runtime->IsJumpRequested();

            // A copy's jump is one its owner's horse already made: the
            // native gate -- rider mounted, speed over the jump threshold,
            // not blocked -- was passed over there, and here it can only
            // refuse or defer. Refused, the flag would stay pending and fire
            // as a phantom jump whenever the copy next qualifies; deferred,
            // the copy takes off late and lands elsewhere. So a copy's
            // request goes into the movement request as the native puts
            // it, flag cleared, whatever the copy's own speed or footing say.
            SDK::wh::entitymodule::C_Horse *horse = runtime->GetHorse();
            if (pending && horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                *requestFlags |= static_cast<std::uint64_t>(SDK::CMovementRequest::eMRF_Jump);
                runtime->ClearJumpRequest();
                return;
            }

            // The owner's jumps are counted where its horse leaves the ground
            // (Horse::CaptureLocalState), not here: a request is committed for
            // every press, in the air or refused too, and copies were sent
            // fifteen jumps in three seconds for the few the horse made.
            g_applyPendingJumpRequestOriginal(runtime, requestFlags);
        }

        using UpdateFastStopReaction_t                            = char(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *, float);
        UpdateFastStopReaction_t g_updateFastStopReactionOriginal = nullptr;

        /**
         * Keeps a copy ridden by a remote player from making its own fast stop.
         *
         * The reaction reads the copy's own speed, gait and obstacle distance
         * and moves the ride to state 6, a hard halt. Whether the owner's horse
         * stopped is the owner's machine's to say: run here as well, the copy
         * halted at a wall the owner rode past, or ran on past a horse the
         * owner stopped at. The request it would have consumed is dropped
         * as it would have been: the rider copy's replayed brake raises it
         * every frame, and left standing it stops the horse at once the
         * moment this machine's player takes the reins.
         */
        char __fastcall UpdateFastStopReaction_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, float frameTime) {
            SDK::wh::entitymodule::C_Horse *horse = runtime != nullptr ? runtime->GetHorse() : nullptr;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                runtime->ClearFastStopRequest();
                return 0;
            }
            return g_updateFastStopReactionOriginal(runtime, frameTime);
        }

        using UpdateHorseThrowOffRiderState_t                                   = void(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *, float);
        UpdateHorseThrowOffRiderState_t g_updateHorseThrowOffRiderStateOriginal = nullptr;

        /**
         * Keeps a copy ridden by a remote player from throwing its rider.
         *
         * `ShouldThrowOffRider_Hook` answers the scared-horse question, but the
         * throw-off state has triggers of its own and asks the rider sync for
         * ride state 7 itself. On a copy that put the other player on the
         * ground here while they stayed in the saddle on their own machine,
         * and the copy horse was snapped after its owner's every second.
         * The rear and throw requests it would have consumed are dropped
         * instead: the rider copy's input and a script's `RearAndThrowDown`
         * still raise them here, and left standing they rear the horse the
         * moment this machine's player takes the reins.
         */
        void __fastcall UpdateHorseThrowOffRiderState_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, float frameTime) {
            SDK::wh::entitymodule::C_Horse *horse = runtime != nullptr ? runtime->GetHorse() : nullptr;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                runtime->ClearThrowOffRequests();
                return;
            }
            g_updateHorseThrowOffRiderStateOriginal(runtime, frameTime);
        }

        using UpdateGaitTransitionReactions_t                                   = char(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *, float);
        UpdateGaitTransitionReactions_t g_updateGaitTransitionReactionsOriginal = nullptr;

        /**
         * Keeps a copy ridden by a remote player from starting off or pulling
         * up on its own.
         *
         * The pass reads the copy's own speed, gait, reaction and rider. A
         * gallop asked for from about a stand holds the runtime speed at zero
         * for 0.1 s, whatever was seeded into it, then plays the horse's
         * idle2move and moves the ride to state 4, the quick start; slowing
         * through a walk with no gait asked for plays move2idle and state 5.
         * The copy's speed and gait are the owner's, but its rider takes
         * control on this machine's clock -- the mount here ends later -- so
         * the copy picked its own moment: it went into state 4 a third of a
         * second after its mount ended and was snapped onto the owner's horse
         * twice in the second and a half after. Whether the horse started off
         * or pulled up so is the owner's machine's to say; its 4 and 5 travel
         * with the stops and the rears, and their clips are started where
         * they are replayed (`Horse::PresentRemoteState`).
         */
        char __fastcall UpdateGaitTransitionReactions_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, float frameTime) {
            SDK::wh::entitymodule::C_Horse *horse = runtime != nullptr ? runtime->GetHorse() : nullptr;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                return 0;
            }
            return g_updateGaitTransitionReactionsOriginal(runtime, frameTime);
        }

        using UpdateHorseMovementSpeed_t                              = void(__fastcall *)(void *, float);
        UpdateHorseMovementSpeed_t g_updateHorseMovementSpeedOriginal = nullptr;

        /**
         * Keeps a copy's runtime speed at what it was seeded.
         *
         * The native moves the speed toward the commanded gait's own speed
         * every frame (+10 m/s^2, -8, -26 on a fast stop), and
         * `ApplyRuntimePaceAndDelta` reads the result. On a copy the seed is
         * the owner's speed (`Horse::PresentRemoteState`), so that step put
         * a frame-time-sized bias on it -- +0.17 m/s at 60 fps, +0.33 at 30
         * -- that no clamp of the seed could see, and across a style's switch
         * speed it was the trot a walking owner's copy played. The native
         * still runs, for the backing flag it copies and the previous speed
         * it keeps; only its speed is put back.
         */
        void __fastcall UpdateHorseMovementSpeed_Hook(void *speedState, float frameTime) {
            g_updateHorseMovementSpeedOriginal(speedState, frameTime);
            auto *runtime = speedState != nullptr ? *reinterpret_cast<SDK::wh::entitymodule::HorseRuntimeDataLayout **>(speedState) : nullptr;
            float speed   = 0.0F;
            if (runtime != nullptr && KCDC::Features::Player::NativePlayerModule::MountSpeed(runtime->GetHorse(), speed)) {
                runtime->SetSpeed(speed);
            }
        }

        using ApplyRuntimePaceAndDelta_t                              = void(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *, SDK::CMovementRequest *);
        ApplyRuntimePaceAndDelta_t g_applyRuntimePaceAndDeltaOriginal = nullptr;

        /**
         * States the owner's pace on a copy's movement request.
         *
         * The native derives the pace from the style the copy's own runtime
         * speed falls in, with the game's hysteresis, and the MoveSpeed tag
         * is resolved from that pace (`ResolveMoveSpeedTag`). A copy's speed
         * is a seed of the owner's, blended and corrected, so its style could
         * land one off the owner's at every switch speed. The owner's horse
         * already resolved its pace; it travels with the pose, and is put on
         * the request here in its place. The delta move keeps the
         * seeded speed, so the body still travels as the owner's does.
         */
        void __fastcall ApplyRuntimePaceAndDelta_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, SDK::CMovementRequest *request) {
            g_applyRuntimePaceAndDeltaOriginal(runtime, request);
            float pace = 0.0F;
            if (runtime != nullptr && request != nullptr && KCDC::Features::Player::NativePlayerModule::MountPace(runtime->GetHorse(), pace)) {
                request->SetPseudoSpeed(pace);
            }
        }

        using UpdateExhaustedReaction_t                             = std::uint64_t(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout *);
        UpdateExhaustedReaction_t g_updateExhaustedReactionOriginal = nullptr;

        /**
         * Keeps a copy ridden by a remote player from pulling up exhausted.
         *
         * The reaction reads the copy's own soul's stamina. The copy is told
         * its simulator's stamina (`Horse::PresentLife`), but between two
         * writes its own drain runs, and whether the owner's horse is spent
         * is the owner's machine's to say: its pull-up travels as ride state
         * 5 with the other reactions.
         */
        std::uint64_t __fastcall UpdateExhaustedReaction_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime) {
            SDK::wh::entitymodule::C_Horse *horse = runtime != nullptr ? runtime->GetHorse() : nullptr;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                return 0;
            }
            return g_updateExhaustedReactionOriginal(runtime);
        }

        using UpdateHorseCollisionAvoidance_t                                   = void(__fastcall *)(SDK::wh::entitymodule::HorseRuntimeDataLayout **, float, bool);
        UpdateHorseCollisionAvoidance_t g_updateHorseCollisionAvoidanceOriginal = nullptr;

        /**
         * Keeps a copy ridden by a remote player out of collision avoidance.
         *
         * Avoidance runs while the runtime's rider input is active, which a
         * remote rider's replayed input is. It probes ahead with deferred
         * rays and steers the animal off obstacles -- a correction the
         * owner's horse already made, and which arrives here in the rider's
         * stream. Done again on the copy, it steers away from the owner's
         * path and queues rays from a worker thread for every such horse;
         * the singleplayer game only ever has one.
         */
        void __fastcall UpdateHorseCollisionAvoidance_Hook(SDK::wh::entitymodule::HorseRuntimeDataLayout **state, float frameTime, bool skipAvoidance) {
            SDK::wh::entitymodule::C_Horse *horse = (state != nullptr && *state != nullptr) ? (*state)->GetHorse() : nullptr;
            if (horse != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(horse->GetRider())) {
                skipAvoidance = true;
            }
            g_updateHorseCollisionAvoidanceOriginal(state, frameTime, skipAvoidance);
        }

        using RiderPlayerInputUpdate_t                            = void(__fastcall *)(SDK::wh::entitymodule::C_RiderPlayerInput *, SDK::wh::entitymodule::HorseRuntimeDataLayout *, float);
        RiderPlayerInputUpdate_t g_riderPlayerInputUpdateOriginal = nullptr;
        using RequestPlayerMonolog_t                              = bool(__fastcall *)(void *, void *);
        RequestPlayerMonolog_t g_requestPlayerMonologOriginal     = nullptr;
        using PlayHorseFeedbackSound_t                            = void(__fastcall *)(void *, std::uint8_t);
        PlayHorseFeedbackSound_t g_playHorseFeedbackSoundOriginal = nullptr;

        /** The horse feedback sound `ProcessSpurInput` plays on every spur: `special_player_spurs_use`. */
        constexpr std::uint8_t kSpurCueSound = 9;

        /**
         * Runs a remote rider's input update in its remote scope.
         *
         * The horse runs its rider's input from the parallel entity update,
         * on a job worker, for every rider -- a remote rider's copy replays
         * its owner's stick into it. What that update has the local player
         * say or see is this machine's own: the speeding line and the
         * speed-limit tutorial (`BuildHorseMovementRequest`), and the spur's
         * line (`ProcessSpurInput`). The scope keeps them off; the ride it
         * computes -- gait, steering, spur, morale -- still runs.
         */
        void __fastcall RiderPlayerInputUpdate_Hook(SDK::wh::entitymodule::C_RiderPlayerInput *input, SDK::wh::entitymodule::HorseRuntimeDataLayout *runtime, float frameTime) {
            const KCDC::Features::Player::NativePlayerModule::RemoteScope scope(KCDC::Features::Player::NativePlayerModule::IsRemoteExecution() || (input != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(input->GetPlayer())));
            g_riderPlayerInputUpdateOriginal(input, runtime, frameTime);
        }

        /**
         * Keeps a remote rider's lines out of the AI message pool.
         *
         * The line is always the local player's, whoever's ride raised it,
         * and it is posted through the AI messaging system's message pool,
         * which has no lock. Two job workers running two riders' inputs post
         * into it at once and corrupt it; the crash then lands in whichever
         * thread uses the pool next, the main thread included.
         */
        bool __fastcall RequestPlayerMonolog_Hook(void *unused, void *metarole) {
            if (KCDC::Features::Player::NativePlayerModule::IsRemoteExecution()) {
                return false;
            }
            return g_requestPlayerMonologOriginal(unused, metarole);
        }

        /**
         * Keeps a remote rider's spur cue off this machine.
         *
         * Every spur plays `special_player_spurs_use` on the horse: the
         * rider's own cue, a 2D event culled at 65535 m. A remote rider's
         * copy spurs whenever its owner does, so each one rang out at full
         * volume on every machine on the server. The spur runs in the remote
         * scope `RiderPlayerInputUpdate_Hook` opens; the horse's own answer
         * to it (`a_o_horse_overspurred`, culled at 30 m) still plays.
         */
        void __fastcall PlayHorseFeedbackSound_Hook(void *feedback, std::uint8_t sound) {
            if (sound == kSpurCueSound && KCDC::Features::Player::NativePlayerModule::IsRemoteExecution()) {
                return;
            }
            g_playHorseFeedbackSoundOriginal(feedback, sound);
        }

        // The deferred ray queue is one vector of requests with no lock, and
        // the parallel entity update reaches it from `JobWorker_NN`: horse
        // avoidance probes, the player interactor, actor probes. With one
        // player in the world those pushes did not overlap; with every remote
        // body and its horse running the same code, two pushes resize the
        // vector at once and the main thread's update or a horse's cancel
        // walks freed records. Every entry point is serialized here.
        // Recursive, because ray callbacks run inside `Update` and may queue.
        std::recursive_mutex g_rayQueueMutex;

        using RayQueueQueue_t                     = std::uint64_t(__fastcall *)(void *, std::int32_t *, const void *, const void *, const void *);
        RayQueueQueue_t g_rayQueueQueueOriginal   = nullptr;
        using RayQueueQueueNoSkip_t               = std::uint64_t(__fastcall *)(void *, std::uint32_t *, const void *, const void *);
        RayQueueQueueNoSkip_t g_rayQueueQueueNoSkipOriginal = nullptr;
        using RayQueueCancel_t                    = std::uint64_t(__fastcall *)(void *, std::uint32_t *);
        RayQueueCancel_t g_rayQueueCancelOriginal = nullptr;
        using RayQueueUpdate_t                    = std::uint64_t(__fastcall *)(void *, float);
        RayQueueUpdate_t g_rayQueueUpdateOriginal = nullptr;

        std::uint64_t __fastcall RayQueueQueue_Hook(void *queue, std::int32_t *priority, const void *request, const void *receiver, const void *extra) {
            std::lock_guard lock(g_rayQueueMutex);
            return g_rayQueueQueueOriginal(queue, priority, request, receiver, extra);
        }

        std::uint64_t __fastcall RayQueueQueueNoSkip_Hook(void *queue, std::uint32_t *priority, const void *request, const void *receiver) {
            std::lock_guard lock(g_rayQueueMutex);
            return g_rayQueueQueueNoSkipOriginal(queue, priority, request, receiver);
        }

        std::uint64_t __fastcall RayQueueCancel_Hook(void *queue, std::uint32_t *queuedId) {
            std::lock_guard lock(g_rayQueueMutex);
            return g_rayQueueCancelOriginal(queue, queuedId);
        }

        std::uint64_t __fastcall RayQueueUpdate_Hook(void *queue, float frameTime) {
            std::lock_guard lock(g_rayQueueMutex);
            return g_rayQueueUpdateOriginal(queue, frameTime);
        }
    } // namespace
} // namespace KCDC::Features::Horse

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CEntity__RegisterInAISystem), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RegisterInAISystem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_registerInAISystemOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.HorseRiddenUpkeep__SnapToNavMesh), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SnapToNavMesh_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_snapToNavMeshOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__TryPlaceHorseNearPlayer), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::TryPlaceHorseNearPlayer_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_tryPlaceHorseNearPlayerOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__SetPlayerHorse),reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SetPlayerHorse_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_setPlayerHorseOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ShouldHorseThrowOffRider), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::ShouldThrowOffRider_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_shouldThrowOffRiderOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorMovementController__RequestMovement), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RequestMovement_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_requestMovementOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.SubmitHorseMoveCommand), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SubmitHorseMoveCommand_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_submitHorseMoveCommandOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.HorseRuntimeData__ApplyPendingJumpRequest), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::ApplyPendingJumpRequest_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_applyPendingJumpRequestOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HorseDirector__UpdateFastStopReaction), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateFastStopReaction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateFastStopReactionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.UpdateHorseThrowOffRiderState), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateHorseThrowOffRiderState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateHorseThrowOffRiderStateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HorseDirector__UpdateGaitTransitionReactions), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateGaitTransitionReactions_Hook),
            reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateGaitTransitionReactionsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.UpdateHorseCollisionAvoidance), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateHorseCollisionAvoidance_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateHorseCollisionAvoidanceOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.UpdateHorseMovementSpeed), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateHorseMovementSpeed_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateHorseMovementSpeedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.HorseRuntimeData__ApplyRuntimePaceAndDelta), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::ApplyRuntimePaceAndDelta_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_applyRuntimePaceAndDeltaOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HorseDirector__UpdateExhaustedReaction), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::UpdateExhaustedReaction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_updateExhaustedReactionOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerInput__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RiderPlayerInputUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_riderPlayerInputUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.RequestPlayerMonolog), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RequestPlayerMonolog_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_requestPlayerMonologOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.PlayHorseFeedbackSound), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::PlayHorseFeedbackSound_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_playHorseFeedbackSoundOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.DeferredRayQueue__Queue), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RayQueueQueue_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_rayQueueQueueOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.DeferredRayQueue__QueueNoSkip), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RayQueueQueueNoSkip_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_rayQueueQueueNoSkipOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.DeferredRayQueue__Cancel), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RayQueueCancel_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_rayQueueCancelOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.DeferredRayQueue__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RayQueueUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_rayQueueUpdateOriginal));
    },
    "Horse");
