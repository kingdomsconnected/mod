#include "features/player/player.h"
#include "core/obfuscation.h"
#include "features/dice/dice_module.h"
#include "features/pickpocket/pickpocket_module.h"
#include "features/player/player_duel_service.h"
#include "features/player/player_service.h"
#include "features/player/native_player_module.h"
#include "shared/game/item_classes.h"
#include "shared/game/locomotion_bands.h"

#include "sdk/cryengine/action/i_procedural_context.h"
#include "sdk/cryengine/system/c_camera_native.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_ladder.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/playermodule/c_battle_archery.h"

#include "features/cart/cart_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/horse/horse_service.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_missile_service.h"
#include "features/social/social_emote_service.h"
#include "common/actor_dressing.h"
#include "common/module_support.h"
#include "common/spawn/spawn_budget.h"

#include "common/angles.h"
#include "common/vector_math.h"
#include "core/presentation_module.h"

#include "shared/features/player/player_combat_attack.h"

#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/c_tag_definition.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/animation/c_entity_attachment.h"
#include "sdk/cryengine/animation/i_attachment.h"
#include "sdk/cryengine/animation/i_attachment_manager.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/cryengine/physics/pe_action_set_velocity.h"
#include "sdk/cryengine/physics/pe_status_living.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/combatmodule/c_combat_actor_model.h"
#include "sdk/wh/combatmodule/c_combat_player.h"
#include "sdk/wh/combatmodule/e_combat_actor_state_id.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_model.h"
#include "sdk/wh/entitymodule/c_actor_movement_controller.h"
#include "sdk/wh/entitymodule/c_actor_shooting_expansion.h"
#include "sdk/wh/entitymodule/c_actor_speak_man.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/entitymodule/c_anim_action_ai_movement.h"
#include "sdk/wh/entitymodule/c_entity_actions.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_horse_ride_context.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_human_hand_helper.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_rider_expansion.h"
#include "sdk/wh/entitymodule/item_draw.h"
#include "sdk/wh/entitymodule/s_actor_class.h"
#include "sdk/wh/playermodule/c_player_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/i_soul_system.h"
#include "sdk/wh/rpgmodule/s_character_body_description.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "Human";

        /** Width of one item class in the replicated equipment string. */
        constexpr std::size_t kItemClassHexWidth = KCDC::Common::kPackedItemClassWidth;

        /** How far a hand got, for a failure that should be reported once. */
        enum class HandItemStage : std::uint8_t {
            None,
            MissingHandHelper,
            MissingItemAttachmentManager,
            Holstered,
            HolsterItemResolutionFailed,
            WaitingInventoryRuntime,
            MaterializeFailed,
            ShadowEquipped,
            LostShadow,
            EquipRefused,
            ItemHandlingRejected,
            AttachFailed,
            TransitionPending,
            Drawn,
        };

        // Physics reports flying on every step off a stair; only this long in
        // the air is a fall or a jump worth telling everyone about.
        constexpr float kAirborneDebounceSeconds = 0.2F;

        // Beyond these the remote body is put on the wire pose outright:
        // CNetLerper::SSettings' snapDistXY, snapDistZ and snapDistZInAir.
        constexpr float kSnapDistanceXY     = 2.5F;
        constexpr float kSnapDistanceZ      = 3.0F;
        constexpr float kSnapDistanceZInAir = 6.0F;

        // CNetLerper::SSettings below the snap: the horizontal error to the
        // drawn pose is closed over lookAhead (0.1 s) at no more than
        // maxInterSpeed (12 m/s), and a stopped body within minLerpDistance
        // (2 cm) is left where it is.
        constexpr float kLerpLookAheadSeconds = 0.1F;
        constexpr float kMaxLerpSpeed         = 12.0F;
        constexpr float kMinLerpDistance      = 0.02F;

        // A correction step reaches the body frames after it is stored: the
        // next frame's movement pass forces it into the animated character's
        // pre-physics merge (NativePlayerModule::SubmitCharacterMoveRequest),
        // which hands physics the merged move as a velocity over that frame,
        // and the position read here shows it once physics has stepped. In
        // the fights logged a body answered 90 ms late at 27 ms frames,
        // 140 ms at 36 ms and 180 ms at 41 ms: about four frames, whatever
        // their length. Closed over lookAhead with four frames of steps
        // under way before the first one showed, the error ran past the pose
        // and circled it every ~0.6 s for seconds. So the steps of the last
        // three frames count as under way and only what they leave is
        // closed, and a frame closes no more of that than lookAhead's share
        // at 25 fps: with it the error settles for a delay of one to five
        // frames at any frame rate, where four is assumed.
        constexpr float kMaxLerpFractionPerFrame = 0.4F;

        // A body up from a fall stands apart from its owner's: the two ragdolls
        // tumbled their own ways, and the observer logs measured 4.2-6.0 m
        // once the copy got up. Until this long after the fall's ragdoll ends,
        // the gap is closed at no more than this speed rather than snapped: 6 m
        // in two seconds, where kMaxLerpSpeed crosses it in half of one. Only a
        // gap the pose buffer itself takes for a teleport
        // (`PoseBufferConfig().warpBase`) is snapped meanwhile. The window
        // covers the stand-up clip and the walk after it with room to spare;
        // too long only walks a real jump inside it.
        constexpr MafiaNet::Time kFallRecoveryMs = 5000;
        constexpr float kFallRecoveryLerpSpeed   = 3.0F;

        // A copy in the air closes what is left between it and its owner over
        // this long, at no more than this much faster than the owner moves.
        constexpr float kAirSteerSeconds  = 0.25F;
        constexpr float kAirSteerMaxSpeed = 3.0F;

        // How long a replicated jump may wait for this body to land from the
        // one before it; past this the arc has gone by and the jump is dropped.
        constexpr MafiaNet::Time kJumpPendingMs = 500;

        // How long a replayed key press this body refused waits before it is
        // pressed again, as a held key would be.
        constexpr MafiaNet::Time kInputRetryMs = 150;

        // How long an owner's block press this body has not shown yet is
        // still pressed: past this the swing it met has gone by.
        constexpr MafiaNet::Time kGuardPressBudgetMs = 600;

        /**
         * The part a soul worn as the other gender wears where the appearance
         * names none: the head comes from ClothingConfig for that gender.
         * The female defaults are the catalog roots `f_body_pale_01` and
         * `f_hair_004`. The female tree has no beard, and nothing is
         * forced where the man's has no single default -- an empty answer
         * leaves the soul's own.
         */
        std::string_view DefaultPartForGender(KCDC::Shared::Game::AppearancePart part, KCDC::Shared::Game::AppearanceGender gender) {
            const bool female = gender == KCDC::Shared::Game::AppearanceGender::Female;
            switch (part) {
            case KCDC::Shared::Game::AppearancePart::Head: return KCDC::Shared::Game::DefaultAppearanceHead(gender);
            case KCDC::Shared::Game::AppearancePart::Body: return female ? std::string_view("f_body_pale_01") : std::string_view();
            case KCDC::Shared::Game::AppearancePart::Hair: return female ? std::string_view("f_hair_004") : std::string_view();
            default: return {};
            }
        }

        // CRC-32 of "female", the tag player_female_body.cpp appends to
        // kcd_male_tags.xml. Mannequin finds tags by their lowercase name's
        // CRC, which is what CTagDefinition::FindCRC takes.
        constexpr std::uint32_t kFemaleTagCrc = 0x4A748A6C;

        // Mode 3 is used by the native generic hand-item path.
        constexpr std::uint8_t kHeldItemHandlingMode = 3;

        /** How long a queued grab is left to enter before it is asked for again. */
        constexpr MafiaNet::Time kCarryGrabTimeoutMs = 1000;

        /**
         * How long an item carry is left to enter before it is asked for again.
         * Longer than a grab's: every ask makes an item.
         */
        constexpr MafiaNet::Time kItemCarryRetryMs = 3000;

        /** How many times one carry is asked for before this body gives up on it. */
        constexpr std::uint8_t kMaxItemCarryAttempts = 3;

        /** How long a queued weapon action is left to play before re-asking. */
        constexpr MafiaNet::Time kWeaponActionTimeoutMs = 1500;

        /** How often the owner reads how worn its pieces are; see equipmentWear. */
        constexpr MafiaNet::Time kWearCaptureMs = 1000;

        SDK::wh::entitymodule::C_Item *FindInventoryItemByWuid(SDK::wh::entitymodule::C_Inventory *inventory, std::uint64_t wuid) {
            if (inventory == nullptr || wuid == 0) {
                return nullptr;
            }
            for (std::size_t i = 0; i < inventory->GetItemCount(); ++i) {
                auto *item = inventory->GetItemAt(i);
                if (item != nullptr && item->GetId() == wuid) {
                    return item;
                }
            }
            return nullptr;
        }

        /** The local body's equipment manager, or null before it exists. */
        SDK::wh::entitymodule::C_EquipmentManager *LocalEquipmentManager() {
            auto *actor   = static_cast<SDK::wh::entitymodule::C_Actor *>(KCDC::Common::LocalHumanActor());
            auto *runtime = actor != nullptr ? actor->GetInventorySoulRuntime() : nullptr;
            return runtime != nullptr && runtime->IsInitialisedFor(actor->GetSoul()) ? runtime->GetEquipmentManager() : nullptr;
        }

    } // namespace

    // --- Replica3 hooks ------------------------------------------------------

    void Human::OnConstructed() {
        _deathStateDirty   = true;
        _equipmentDirty    = true;
        _receivedEquipment = equipment;

        // The missile state is edge-driven from the shooting action tree, and
        // a session can begin with a bow already drawn -- so this body needs
        // one read of its own before any of those edges can fire.
        if (IsOwner()) {
            KCDC::Core::gApplication->GetPlayerMissileService().SeedLocalState(*this);
        }

        Framework::Logging::GetLogger(kLogger)->info("Human replica constructed: networkId={}, nickname='{}', slot={}, local={}", GetNetworkID(), nickname, playerIndex, IsOwner());
    }

    void Human::OnDeserialized(bool transformUpdated) {
        // The velocity the pose below is buffered with is measured there.
        Shared::Entities::HumanEntity::OnDeserialized(transformUpdated);
        RecordCombatSample();

        if (equipment != _receivedEquipment) {
            _receivedEquipment = equipment;
            _equipmentDirty    = true;
        }
        if (equipmentWear != _receivedEquipmentWear) {
            _receivedEquipmentWear = equipmentWear;
            _wearDirty             = true;
        }

        // A refusal was about the appearance that earned it.
        if (!(appearance == _appliedAppearance)) {
            _appearanceDirty   = true;
            _appearanceRefused = false;
        }

        if (IsOwner()) {
            return;
        }

        // What `C_Soul::HasPerk` answers for this copy; the service keeps a
        // copy for the game's worker threads and republishes it on a change.
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetPlayerProgressionService().NoteRemotePerks(GetNetworkID(), progression.perks);
        }

        _deathStateDirty = true;
        RecordCombatSample();

        const auto noteHandClass = [&](std::size_t hand) {
            if (_receivedHandStateKnown[hand] && handItemClass[hand] == _receivedHandClass[hand]) {
                return;
            }
            _receivedHandStateKnown[hand] = true;
            _receivedHandClass[hand]      = handItemClass[hand];
            _handCreateAttempts[hand]     = 0;
            _remoteHandStage[hand]        = static_cast<std::uint8_t>(HandItemStage::None);
        };
        noteHandClass(Shared::Entities::HumanEntity::kRightHand);
        noteHandClass(Shared::Entities::HumanEntity::kLeftHand);

        if (!transformUpdated || !transformReady || poseIncarnation != soul.incarnation) {
            return;
        }

        // Buffer transform and velocity together for native movement replay.
        KCDC::Common::Replication::PoseSnapshot pose;
        pose.position        = position;
        pose.velocity        = velocity;
        pose.rotation        = rotation;
        pose.inAir           = inAir;
        pose.input           = input;
        pose.fallSequence    = fallSequence;
        SampleLookPitch(pose);
        _snapshots.Push(pose, GetLastTransformTime());
    }

    void Human::DeallocReplica(MafiaNet::Connection_RM3 *) {
        KCDC::Core::gApplication->GetPlayerService().QueueBodyDespawn(GetNetworkID(), _entityId);
        ForgetBodyActions();
        delete this;
    }

    // --- This body -----------------------------------------------------------

    void Human::Frame() {
        if (IsOwner()) {
            // The ticket counts as well as the body: ownership can arrive while
            // a request is still queued, and that body must not be built.
            if (_entityId != 0 || _spawnTicket != nullptr) {
                Despawn();
            }
            CaptureLocalState();
        }
        else {
            _captureActorId  = 0;
            _captureSoulWuid = 0;
            PresentRemoteState();
        }
    }

    SDK::wh::entitymodule::C_Actor *Human::ResolveActor() const {
        return _nativePlayer;
    }

    SDK::wh::entitymodule::C_Actor *Human::ResolveBody() const {
        return IsOwner() ? KCDC::Common::LocalHumanActor() : ResolveActor();
    }

    Human *Human::FindLocal() {
        return KCDC::Common::FindReplica<Human>([](Human *human) {
            return human->IsOwner();
        });
    }

    SDK::EntityId Human::ResolveBodyEntityId() const {
        return IsOwner() ? KCDC::Common::LocalActorEntityId() : _entityId;
    }

    void Human::BindNativePlayer() {
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        _nativePlayer     = _entityId != 0 && actorSystem != nullptr ? static_cast<SDK::wh::entitymodule::C_Player *>(actorSystem->GetActor(_entityId)) : nullptr;
    }

    void Human::Despawn() {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(KCDC::Features::Player::NativePlayerModule::IsRemote(_nativePlayer));
        // Dropped first, so a body that was still queued is never made for a
        // player who has already gone.
        ReleaseSpawnTicket();

        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();
        CancelRemoteAttack(actor != nullptr ? actor->GetCombatActor() : nullptr);
        UnbindSoul();
        KCDC::Core::gApplication->GetPlayerMissileService().ForgetActor(GetNetworkID());
        KCDC::Core::gApplication->GetEmoteService().ForgetActor(GetNetworkID());
        KCDC::Core::PresentationModule::Get().ForgetActor(GetNetworkID());
        _stanceRequested              = false;
        _owedGuardPresses             = 0;
        _combatPresentationSuppressed = false;

        // The poses buffered for the body that goes go with it, whether or not
        // one was ever built: a presentation reset that finds no body still
        // has to start the next one from the wire rather than from them.
        _snapshots.Clear();
        _presentedPoseTime = 0;
        _jumpCountKnown    = false;
        _riderPressesKnown = false;
        _jumpPendingUntil  = 0;
        _correctionSteps   = {};
        _seenOnFoot        = false;
        if (_entityId == 0) {
            return;
        }

        KCDC::Core::gApplication->GetHorseService().ForgetSeatsFor(_entityId);

        // A carry must not outlive either of its bodies: the carrier's
        // controller would keep driving a body that is gone, or a body would
        // stay bound to a controller that is.
        KCDC::Core::gApplication->GetPlayerService().ReleaseCarriesOf(_entityId);
        KCDC::Core::gApplication->GetPlayerService().ReleaseCarryBy(_entityId);
        KCDC::Core::gApplication->GetPlayerService().ReleaseItemCarryBy(_entityId);

        // A pocket being picked on this body is ended while the body the loot
        // screen reads from is still here.
        KCDC::Features::Pickpocket::PickpocketModule::Get().ForgetVictim(_entityId);

        // So is a dice table that holds this body as its opponent.
        KCDC::Features::Dice::DiceModule::Get().ForgetBody(_entityId);

        // And a lean: its Finish frees the slot, which no other body could
        // lean out of again while it named this one.
        auto *lean = actor != nullptr && _leanStarted ? SDK::wh::playermodule::C_BattleArchery::Of(actor, false) : nullptr;
        if (lean != nullptr && lean->IsActive()) {
            lean->Finish();
        }

        WithdrawLocomotion();
        // Unregister pickup playback before the skeleton/controller dies.
        ForgetBodyActions();
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().DespawnNow(_entityId);
            Framework::Logging::GetLogger(kLogger)->info("Despawned the body of '{}' (entity {})", nickname, _entityId);
        }
        _entityId       = 0;
        _nativePlayer   = nullptr;
        _boundCharacter = nullptr;
        _equipmentDirty = true;
        _weaponsPending = false;

        // The look-as layer went with the soul, and the disguise with the entity.
        ForgetDisguise();
        // Anything this body carried is its own entity and can outlive it, so
        // what was hidden is handed back before the body it hung on is gone.
        ShowCarriedEntities();
        _drawHeldByHide     = false;
        _appliedBodyHidden  = false;
        _visibilityEntityId = 0;
        _appliedAppearance = {};
        _lookAsParts       = {};
        _soulGenderKnown   = false;
        _appearanceDirty   = true;
        _appearanceRefused = false;
        ResetHandState(Shared::Entities::HumanEntity::kRightHand);
        ResetHandState(Shared::Entities::HumanEntity::kLeftHand);
        _ladderGrabGuid               = 0;
        _ladderGrabAttempts           = 0;
        _ladderGrabAt                 = 0;
        _ladderExitRequested          = false;
        _leanStartGuid                = 0;
        _leanStartSlot                = 0;
        _leanStarted                  = false;
        _carrySeen                   = false;
        _carryStreamedIn              = false;
        _carryGrabAskedFor            = 0;
        _itemCarryAskedAt             = 0;
        _itemCarryAttempts            = 0;
        _itemCarryAttemptClass        = {};
        _itemPutDownAsked             = false;
        _combatTimelineHead           = 0;
        _combatTimelineCount          = 0;
        _owedGuardPresses             = 0;
        _jumpCountKnown      = false;
        _riderPressesKnown            = false;
        _jumpPendingUntil    = 0;
        _presentedPoseTime   = 0;
        _snapshots.Clear();
    }

    // --- Owner capture -------------------------------------------------------

    void Human::CaptureLocalState() {
        CaptureSoulState();
        visorClosed  = false;
        auto *player = KCDC::Common::LocalPlayer();
        if (player == nullptr) {
            return;
        }

        // The body this client plays wears what it publishes. CaptureSoulState
        // marks a body change; this is where the new one can be reached.
        //
        // A refusal here has to walk the published value back, because
        // AdoptAppearance cannot: with no local actor it has nothing to try
        // the appearance against, so it accepts and defers to this. Backing
        // out to what the body is actually wearing keeps the invariant that
        // nobody is shown something its owner is not.
        if (_appearanceDirty && !_appearanceRefused) {
            if (auto *localActor = KCDC::Common::LocalHumanActor()) {
                if (!ApplyAppearance(localActor->GetSoul()) && !(appearance == _appliedAppearance)) {
                    Framework::Logging::GetLogger(kLogger)->warn("Withdrawing an appearance this body cannot wear");
                    appearance = _appliedAppearance;
                }

                // The soul now wears the gender; the skeleton follows it at
                // the next animation sync, the only safe point to swap it.
                KCDC::Core::gApplication->GetPlayerService().RequestLocalBody(_appliedAppearance.gender);
            }
        }

        // And what it is drawn as instead, which rides the same entity.
        ApplyDisguise(player);

        // And whether it is drawn at all.
        ApplyBodyVisibility(player, KCDC::Common::LocalHumanActor());

        // Equip hooks mark possible changes; fingerprint only the local
        // inventory.
        bool outfitChanged = false;
        if (KCDC::Core::gApplication->GetPlayerService().ConsumeEquipmentChanged()) {
            if (const std::uint64_t worn = FingerprintWornEquipment(); worn != _wornEquipmentFingerprint) {
                _wornEquipmentFingerprint = worn;
                equipment                 = CaptureWornEquipment();
                outfitChanged             = true;
                Framework::Logging::GetLogger(kLogger)->info("Local equipment changed: {} item class(es)", equipment.size() / kItemClassHexWidth);
            }
        }

        // And how worn those pieces are. Dirt, blood and health come and go
        // with no equip event to mark them, so they are read on a timer -- and
        // with every new outfit, so the two strings always name the same
        // pieces. The state channel sends a field only when it differs from
        // what it last sent.
        if (const MafiaNet::Time now = MafiaNet::GetTime(); outfitChanged || now - _wearCapturedAt >= kWearCaptureMs) {
            _wearCapturedAt = now;
            equipmentWear   = KCDC::Common::CaptureEquipmentWear(KCDC::Common::LocalHumanActor(), equipment);
        }

        // A fall this body took (its horse struck a wall), for every copy to take too.
        fallSequence = KCDC::Features::Player::NativePlayerModule::LocalFalls();
        {
            const SDK::Vec3 &hit = KCDC::Features::Player::NativePlayerModule::LocalFallHitOffset();
            fallHitX             = hit.x;
            fallHitY             = hit.y;
            fallHitZ             = hit.z;
        }

        // The actor's closed flag can survive taking a helmet off. Require
        // a worn visor too, and sample every tick: toggling it is not an
        // equipment change. Publish state so late listeners hear it correctly.
        if (auto *actor = KCDC::Common::LocalHumanActor(); actor != nullptr && actor->GetActorModel() != nullptr && actor->IsVisorClosed()) {
            auto *manager = LocalEquipmentManager();
            visorClosed   = manager != nullptr && manager->GetEquippedVisorTypeId() != 0;
        }

        const SDK::Vec3 worldPos = player->GetWorldPos();
        const SDK::Quat worldRot = player->GetWorldRotation();

        const glm::vec3 captured(worldPos.x, worldPos.y, worldPos.z);
        if (!KCDC::Common::IsFinite(captured)) {
            return;
        }

        // Commanded movement velocity, for the receiver's extrapolation and
        // drift correction; physics velocity lags stance and pace changes.
        velocity = glm::vec3(0.0F);
        if (auto *localActor = KCDC::Common::LocalHumanActor()) {
            const glm::vec3 movementVelocity = KCDC::Common::ToGlm(localActor->GetMovementVelocity());
            if (KCDC::Common::IsFinite(movementVelocity)) {
                velocity = movementVelocity;
            }
        }

        // What the owner pressed rather than what came of it: the receiver's
        // own handlers and PreUpdate turn this into the same acceleration,
        // pace, tags and horse gait.
        input.moveX                                      = 0;
        input.moveY                                      = 0;
        input.run                                        = true;
        input.sprint                                     = false;
        auto *localPlayer                                = static_cast<SDK::wh::entitymodule::C_Player *>(KCDC::Common::LocalHumanActor());
        SDK::wh::entitymodule::C_PlayerInput *localInput = localPlayer != nullptr ? localPlayer->GetPlayerInput() : nullptr;
        if (localInput != nullptr) {
            // The raw axes every move handler writes: on foot they are the move
            // vector, in the saddle the stick before the riding curve, which
            // the receiver's own stick handler applies again.
            SDK::Vec2 axes = localInput->GetRawMovementInput();
            // Orbiting on foot, the keys name a direction around the camera:
            // the body turns to it and walks forward (Camera::OrbitCamera
            // rewrites the move vector). Sending the keys would have every
            // copy strafe or back-pedal while it travels forward, so the
            // copies get the move vector the body actually walks.
            const bool saddled = localPlayer->GetRiderPlayerControl()->GetRiderInput() != nullptr && localPlayer->GetRiderPlayerControl()->GetRiderInput()->GetHorse() != nullptr;
            if (!saddled && KCDC::Core::gApplication->GetCameraControl().IsOrbiting()) {
                const SDK::Vec3 &walked = localInput->GetMovementInput();
                axes                    = SDK::Vec2 {walked.x, walked.y};
            }
            if (std::isfinite(axes.x) && std::isfinite(axes.y)) {
                input.moveX = static_cast<std::int8_t>(std::lround(std::clamp(axes.x, -1.0F, 1.0F) * Shared::Entities::PlayerInput::kMoveInputScale));
                input.moveY = static_cast<std::int8_t>(std::lround(std::clamp(axes.y, -1.0F, 1.0F) * Shared::Entities::PlayerInput::kMoveInputScale));
            }
            input.run    = localInput->IsRunMode() || localInput->IsAnalogMovement();
            input.sprint = localInput->IsSprinting();
        }
        // In the saddle the gait keys never reach the player input: its
        // OnAction offers every action to the rider input first, whose own
        // handlers take sprint and toggle_run. What they latched is the gait
        // the owner asked for.
        SDK::wh::entitymodule::C_RiderPlayerInput *riderInput = localPlayer != nullptr ? localPlayer->GetRiderPlayerControl()->GetRiderInput() : nullptr;
        input.inSaddle                                        = riderInput != nullptr && riderInput->GetHorse() != nullptr;
        if (input.inSaddle) {
            input.run    = !riderInput->IsWalkToggled();
            input.sprint = riderInput->IsSprintHeld();
            // Every press with a value stamps its time, so a new stamp is a
            // new press.
            if (riderInput->GetLastSprintPressTime() != _capturedSprintPressTime) {
                _capturedSprintPressTime = riderInput->GetLastSprintPressTime();
                input.sprintPresses      = static_cast<std::uint8_t>((input.sprintPresses + 1) & Shared::Entities::PlayerInput::kCountMask);
            }
        }
        input.jumps           = KCDC::Core::gApplication->GetPlayerService().LocalJumpCount();
        input.slowdownPresses = KCDC::Core::gApplication->GetPlayerService().LocalSlowdownCount();

        // Physics still answers the one question it is the authority on.
        inAir = false;
        if (auto *physics = player->GetPhysics()) {
            SDK::pe_status_living status {};
            if (physics->GetStatus(&status) != 0) {
                const glm::vec3 travel = KCDC::Common::ToGlm(status.vel) - KCDC::Common::ToGlm(status.velGround);
                if (KCDC::Common::IsFinite(travel)) {
                    velocity = glm::clamp(travel, -glm::vec3(kMaxGroundSpeed, kMaxGroundSpeed, kMaxVerticalSpeed), glm::vec3(kMaxGroundSpeed, kMaxGroundSpeed, kMaxVerticalSpeed));
                }
                inAir = status.bFlying != 0 && status.timeFlying >= kAirborneDebounceSeconds;
            }
        }


        position        = captured;
        rotation        = glm::quat(worldRot.w, worldRot.x, worldRot.y, worldRot.z);
        transformReady  = soul.valid;
        poseIncarnation = soul.incarnation;

        CaptureLocomotionTags();
        CaptureBodyActions(KCDC::Common::LocalHumanActor());
        CaptureStation(KCDC::Common::LocalHumanActor());
        CaptureLadder();
        CaptureLean();
        CaptureCombat();

        // The held torch is not sampled here; SetHandItem reports its edges.
        CaptureHeadLook(KCDC::Common::LocalHumanActor());
    }

    void Human::CaptureLadder() {
        ladderGuid       = 0;
        ladderClimbInput = 0.0F;

        auto *localActor = KCDC::Common::LocalHumanActor();
        auto *action     = localActor != nullptr ? KCDC::Core::gApplication->GetPlayerService().LadderActionFor(localActor->GetEntityId()) : nullptr;
        if (action == nullptr) {
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *ladder       = entitySystem != nullptr ? entitySystem->GetEntity(action->GetLadderEntityId()) : nullptr;
        if (ladder == nullptr) {
            return;
        }

        // A ladder placed in the level carries a guid; one the game spawned
        // does not, and there is nothing a receiver could look up.
        ladderGuid = ladder->GetGuid();
        if (ladderGuid == 0) {
            return;
        }

        // The climb's own speed, not its input register: the player climbs
        // through C_PlayerInput's movement request, which leaves the
        // register at 0. A remote body is handed this as its input, which
        // the ladder action turns into the same forward request.
        const float climbInput = action->GetClimbVelocity();
        if (std::isfinite(climbInput)) {
            ladderClimbInput = std::clamp(climbInput, -Shared::Entities::HumanEntity::kMaxLadderClimbInput, Shared::Entities::HumanEntity::kMaxLadderClimbInput);
        }
    }

    void Human::CaptureLean() {
        battlementGuid = 0;
        battlementSlot = 0;

        // Only the lean: shooting from a battlement is not replicated.
        auto *localActor = KCDC::Common::LocalHumanActor();
        auto *lean       = localActor != nullptr ? SDK::wh::playermodule::C_BattleArchery::Of(localActor, false) : nullptr;
        if (lean == nullptr || !lean->IsActive() || !lean->IsLeaning()) {
            return;
        }

        // A battlement placed in the level carries a guid; one the game
        // spawned does not, and there is nothing a receiver could look up.
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(lean->GetSlot()->GetBattlementEntityId()) : nullptr;
        auto *battlement   = SDK::wh::playermodule::C_Battlement::Find(entity);
        const std::int32_t slot = battlement != nullptr ? battlement->IndexOf(lean->GetSlot()) : -1;
        if (slot < 0 || slot > UINT8_MAX || entity->GetGuid() == 0) {
            return;
        }
        battlementGuid = entity->GetGuid();
        battlementSlot = static_cast<std::uint8_t>(slot);
    }

    void Human::CaptureLocomotionTags() {
        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *localActor = IsOwner() ? static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr) : GetPlayer();
        if (localActor == nullptr) {
            return;
        }

        SDK::wh::entitymodule::C_ActorModel *actorModel = localActor->GetActorModel();
        if (IsOwner()) {
            input.crouch = actorModel != nullptr && actorModel->IsCrouching();
        }

        auto *tagState = localActor->GetTagState();
        if (tagState == nullptr) {
            return;
        }

        const auto pack = [](std::uint32_t tag) -> std::uint8_t {
            return tag == SDK::CTagDefinition::kInvalidId || tag > 0xFEU ? Shared::Entities::HumanEntity::kNoTag : static_cast<std::uint8_t>(tag);
        };

        const std::uint8_t capturedMoveSpeedTag = pack(tagState->GetGroupTag(localActor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_SPEED)));
        const std::uint8_t capturedMoveDirTag   = pack(tagState->GetGroupTag(localActor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_DIR)));
        const std::uint8_t capturedStanceTag    = pack(tagState->GetGroupTag(localActor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_STANCE)));
        moveSpeedTag                            = capturedMoveSpeedTag;
        moveDirTag                              = capturedMoveDirTag;
        stanceTag                               = capturedStanceTag;
    }

    void Human::CaptureHeadLook(SDK::wh::entitymodule::C_Actor *actor) {
        headLookReady  = false;
        auto *movement = actor != nullptr ? actor->GetMovementController() : nullptr;
        if (movement == nullptr) {
            return;
        }

        SDK::SMovementState state {};
        movement->GetMovementState(state, true);
        glm::vec3 direction(state.eyeDirection.x, state.eyeDirection.y, state.eyeDirection.z);
        // Leaning out, sitting or in the saddle, the movement state's eye is
        // the body's own: the body is held and the camera turns alone (the
        // lean's camera limit, C_BattleArchery::ApplyArcheryStateControls
        // 0x182E5A340). What the owner looks at is then the view camera's
        // forward.
        if (battlementGuid != 0 || bodyStance.stance == Shared::Game::BodyStance::Sitting || KCDC::Core::gApplication->GetHorseService().SeatOf(actor->GetEntityId()) != 0) {
            const SDK::Matrix34 &view = SDK::ISystem::GetInstance()->GetViewCamera().GetMatrix();
            direction                 = glm::vec3(view.m[0][1], view.m[1][1], view.m[2][1]);
        }
        // Orbiting, the body is held as well and the camera turns alone
        // (Camera::OrbitCamera). What the head follows is the camera's
        // direction bounded to what a neck does, and that bound is the
        // orbit's own figure, so every copy's head turns as far as this one.
        else if (KCDC::Core::gApplication->GetCameraControl().IsOrbiting()) {
            direction = KCDC::Common::ToGlm(KCDC::Core::gApplication->GetCameraControl().GetOrbitHeadLook());
        }
        const float length = glm::length(direction);
        if (!KCDC::Common::IsFinite(direction) || !std::isfinite(length) || length < 0.001F) {
            return;
        }

        headLookDirection = direction / length;
        headLookReady     = true;
    }

    void Human::CaptureCombat() {
        const PlayerService::LocalCombat &combat = KCDC::Core::gApplication->GetPlayerService().Combat();
        if (combatRevision != combat.combatRevision) {
            Framework::Logging::GetLogger(kLogger)->debug("Publishing combatRevision={} (was {})", combat.combatRevision, combatRevision);
        }
        combatRevision = combat.combatRevision;
        fistsUp                                  = combat.fistsUp;
        guardRequest                             = combat.guardRequest;

        // -1 survives the narrowing; anything wider is a row we do not have.
        combatZone = combat.combatZone >= 0 && combat.combatZone <= 0x7FFF ? static_cast<std::int16_t>(combat.combatZone) : static_cast<std::int16_t>(-1);

        // Whom this player's own opponent picker locked on to; every copy
        // of this body is handed the same lock.
        combatTargetNetworkId = KCDC::Core::gApplication->GetPlayerDuelService().LocalTargetNetworkId();
    }

    std::uint64_t Human::FingerprintWornEquipment() {
        auto *manager = LocalEquipmentManager();
        if (manager == nullptr) {
            return 0;
        }

        auto *torchItem       = manager->GetWeaponSlotItem(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch);
        std::uint64_t mixed   = 0xCBF29CE484222325ULL;
        const std::size_t end = manager->GetCarriedCount();
        for (std::size_t i = 0; i < end; ++i) {
            auto *item = manager->GetCarriedAt(i);
            if (item == nullptr || item == torchItem) {
                continue;
            }
            const auto itemClass = item->InvokeGetItemClassRaw();
            for (const std::uint64_t half : {itemClass.m_low, itemClass.m_high}) {
                mixed = (mixed ^ half) * 0x100000001B3ULL;
            }
        }
        return mixed;
    }

    std::string Human::CaptureWornEquipment() {
        auto *manager = LocalEquipmentManager();
        if (manager == nullptr) {
            return {};
        }

        // Walked in place; nothing the engine allocated crosses back out.
        std::string packed;
        const std::size_t count = manager->GetCarriedCount();
        packed.reserve(count * kItemClassHexWidth);
        auto *torchItem = manager->GetWeaponSlotItem(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch);
        for (std::size_t i = 0; i < count; ++i) {
            auto *item = manager->GetCarriedAt(i);
            if (item == nullptr || item == torchItem) {
                continue;
            }
            KCDC::Common::AppendPackedItemClass(packed, item->InvokeGetItemClassRaw());
        }

        return packed;
    }

    // --- Presentation --------------------------------------------------------

    float Human::RemoteCorrectionSpeedLimit(SDK::wh::entitymodule::C_Actor *actor, const Shared::Entities::PlayerInput &input, float errorLength, float snapDistance, float lerpSpeed) {
        // The travel speed the body's own movement asked of its blendspace
        // this frame: CAnimatedCharacter::SetDesiredLocalLocation
        // (0x18083B82C) writes it as eMotionParamID_TravelSpeed into the
        // bound third-person character's skeleton, from the requested
        // movement alone -- the correction is forced movement it never sees.
        SDK::CAnimatedCharacter *animated = actor != nullptr ? actor->GetAnimatedCharacterRuntime() : nullptr;
        SDK::ISkeletonAnim *skeleton      = animated != nullptr ? animated->GetBoundSkeletonAnim3P() : nullptr;
        float travelSpeed                 = 0.0F;
        if (skeleton == nullptr || !skeleton->GetDesiredMotionParam(SDK::eMotionParamID_TravelSpeed, travelSpeed) || !std::isfinite(travelSpeed)) {
            travelSpeed = 0.0F;
        }
        const Shared::Game::LocomotionGait gait  = Shared::Game::GaitForInput(input.run, input.sprint, input.crouch);
        const Shared::Game::LocomotionBand *band = Shared::Game::FindLocomotionBand(_bodyGender == Shared::Game::AppearanceGender::Female, gait);
        const float limit                        = Shared::Game::CorrectionSpeedLimit(band, travelSpeed, errorLength, snapDistance, kMaxLerpSpeed);

        // What the in-game check compares: the stride's speed, the band it
        // plays in and the slide the cap kept off it. Once a second at most.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (lerpSpeed > limit && now - _correctionReportedAt >= 1000) {
            _correctionReportedAt = now;
            Framework::Logging::GetLogger(kLogger)->debug("Correction capped player={} wanted={:.2f} limit={:.2f} travel={:.2f} band={:.2f}-{:.2f} error={:.2f}", GetNetworkID(), lerpSpeed, limit, travelSpeed, band != nullptr ? band->minSpeed : 0.0F, band != nullptr ? band->maxSpeed : 0.0F, errorLength);
        }
        return limit;
    }

    void Human::PresentRemoteState() {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(true);
        // A frame stores at most one step, and one that returns before the
        // correction stores none.
        _correctionSteps = {glm::vec2(0.0F), _correctionSteps[0], _correctionSteps[1], _correctionSteps[2]};
        if (!soul.valid) {
            Despawn();
            return;
        }
        if (_presentationIncarnation != soul.incarnation || _presentationOwner != ownerGUID || _presentationEpoch != stateEpoch) {
            if (_deathHandled && _entityId != 0 && _presentationOwner == ownerGUID && _presentationEpoch == stateEpoch) {
                KCDC::Core::gApplication->GetPlayerMissileService().ForgetActor(GetNetworkID());
                KCDC::Core::gApplication->GetEmoteService().ForgetActor(GetNetworkID());
                KCDC::Core::PresentationModule::Get().ForgetActor(GetNetworkID());
                _snapshots.Clear();
                _jumpCountKnown               = false;
                _riderPressesKnown            = false;
                _jumpPendingUntil             = 0;
                _correctionSteps              = {};
                _stanceRequested              = false;
                _owedGuardPresses             = 0;
                _combatPresentationSuppressed = false;
                _remoteAttackActive           = false;
                _remoteAttackGeneration       = 0;
                _remoteAttackInputClass       = -1;
                _equipmentDirty               = true;
                _appliedAppearance            = {};
                _lookAsParts                  = {};
                _soulGenderKnown              = false;
                _appearanceDirty              = true;
                _appearanceRefused            = false;
            }
            else {
                Despawn();
            }
            _presentationIncarnation = soul.incarnation;
            _presentationOwner       = ownerGUID;
            _presentationEpoch       = stateEpoch;
            _spawnRefused            = false;
            _nextSpawnAttempt        = 0;
        }
        if (_entityId == 0) {
            RequestSpawn();
            return;
        }

        // The skeleton came with the Properties the body was spawned with, and
        // no soul write moves it. A body whose owner changed gender is rebuilt;
        // the next tick asks for one on the other skeleton.
        if (appearance.gender != _bodyGender) {
            Framework::Logging::GetLogger(kLogger)->info("'{}' (networkId {}) changed gender; rebuilding the body on the other skeleton", nickname, GetNetworkID());
            Despawn();
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(_entityId) : nullptr;
        if (entity == nullptr) {
            UnbindSoul();
            ForgetDisguise();
            ShowCarriedEntities();
            _drawHeldByHide     = false;
            _appliedBodyHidden  = false;
            _visibilityEntityId = 0;
            // The body went with a level change; the next tick re-spawns it.
            KCDC::Core::gApplication->GetPlayerMissileService().ForgetActor(GetNetworkID());
            WithdrawLocomotion();
            ForgetBodyActions();
            _entityId       = 0;
            _nativePlayer   = nullptr;
            _boundCharacter = nullptr;
            _equipmentDirty = true;
            ResetHandState(Shared::Entities::HumanEntity::kRightHand);
            ResetHandState(Shared::Entities::HumanEntity::kLeftHand);
            _stanceRequested              = false;
            _owedGuardPresses             = 0;
            _combatPresentationSuppressed = false;
            _remoteAttackInputClass       = -1;
            _remoteAttackGeneration       = 0;
            _remoteAttackActive           = false;
            _ladderGrabGuid               = 0;
            _ladderGrabAttempts           = 0;
            _ladderGrabAt                 = 0;
            _ladderExitRequested          = false;
            _leanStartGuid                = 0;
            _leanStartSlot                = 0;
            _leanStarted                  = false;
            _forcedOffSaddle             = false;
            _seenOnFoot                   = false;
            _wasSeated                    = false;
            _carrySeen                    = false;
            _carryStreamedIn              = false;
            _carryGrabAskedFor            = 0;
            _itemCarryAskedAt             = 0;
            _itemCarryAttempts            = 0;
            _itemCarryAttemptClass        = {};
            _itemPutDownAsked             = false;
            _jumpCountKnown               = false;
            _riderPressesKnown            = false;
            _jumpPendingUntil             = 0;
            _correctionSteps              = {};

            _snapshots.Clear();
            return;
        }

        SDK::wh::entitymodule::C_Player *actor = GetPlayer();

        if (!ApplySoulState(actor)) {
            WithdrawLocomotion();
            return;
        }

        // The owner can change what it wears at any time. OnDeserialized
        // marks it; this is where the body can actually be reached.
        // A weapon change waiting on a hand action or ground transfer is
        // retried on a short interval rather than re-dressed every frame.
        const bool weaponRetryDue = _weaponsPending && MafiaNet::GetTime() >= _weaponRetryAt;
        if ((_equipmentDirty || weaponRetryDue) && actor != nullptr && actor->GetSoul() != nullptr) {
            ApplyOutfit(actor);
        }

        // And how worn each piece is, once it is on: a piece this body was
        // dressed in anew is as the game made it, not as the owner's is.
        if (_wearDirty && !_equipmentDirty && actor != nullptr) {
            _wearDirty = false;
            KCDC::Common::ApplyEquipmentWear(actor, equipment, equipmentWear);
        }

        // And the body under the clothes.
        if (_appearanceDirty && !_appearanceRefused && actor != nullptr) {
            ApplyAppearance(actor->GetSoul());
        }
        ApplyDisguise(entity);
        ApplyBodyVisibility(entity, actor);

        // Reconcile against the native model, including a newly spawned body
        // or a state changed by native actions. The native setters invalidate
        // helmet visuals as well as signalling the property; writing the flag
        // alone leaves the old helmet model attached.
        auto *actorModel = actor != nullptr ? actor->GetActorModel() : nullptr;
        if (actorModel != nullptr && actor->IsVisorClosed() != visorClosed) {
            if (visorClosed) {
                actor->CloseVisor();
            }
            else {
                actor->OpenVisor();
            }
        }

        // The engine says whether this body is in an interactive action and
        // what it is interacting with, so nothing here has to track one. A
        // dice table holding the body as its opponent seats and animates it
        // the same way, so it is placed and driven by the table meanwhile.
        const bool doorInteractionActive = (actorModel != nullptr && actorModel->GetInteractionTargetEntityId() != 0) || KCDC::Features::Dice::DiceModule::Get().HoldsBody(_entityId);
        const bool stationOwned          = HasStationPose();
        if (stationOwned || KCDC::Features::Dice::DiceModule::Get().HoldsBody(_entityId)) {
            SuspendBodyActions(actor);
        }
        const bool doorCombatYield = doorInteractionActive || stationOwned;

        // Cooperative animations own movement while their shared action runs.
        //
        // Asked of the body rather than remembered: the clip is an action on
        // its own queue and the engine is what puts it there and takes it out
        // again, which is the same thing CryEngine's cooperative-animation
        // manager answers with `IsActorBusy`. Nothing here can be left holding
        // a body that finished.
        const bool sharedAnimation = actor != nullptr && actor->GetActionActor()->IsInSharedAnimation();

        // IsItemDrawn dereferences the actor model at +0x990; wait until it
        // exists.
        if (actor != nullptr) {
            RefreshAnimationBinding(actor);
            ApplyDeathState(actor);
            if (_deathHandled) {
                ReleaseHeldDecoy();
                WithdrawLocomotion();
                return;
            }
            // A limp body keeps what it holds until it is up. An item action
            // ends its ragdoll on the spot -- the ragdoll action's Exit resets
            // the manager at once unless the stand-up finished it
            // (0x180AA13A8) -- and leaves the body in a bind pose until
            // something else animates it.
            const bool limp = actor->GetRagdollManager() != nullptr && actor->GetRagdollManager()->IsRagdoll();
            if (!doorInteractionActive && !stationOwned && !limp && actor->IsSettled() && _appliedSoul.CanAct()) {
                ApplyHandItems(static_cast<SDK::wh::entitymodule::C_Human *>(actor));
            }
        }

        ApplyCombat(actor, doorCombatYield);
        if (actor != nullptr) {
            ApplyCarry(actor);
            ApplyCarriedItem(actor);
        }

        // Native body actions own movement until they finish.
        const bool actionOwned = !doorInteractionActive && !stationOwned && ApplyBodyActions(actor);
        if (doorInteractionActive && _heldDecoy != nullptr) {
            ReleaseHeldDecoy();
        }

        // Drop riding history BEFORE sampling the first on-foot frame. A
        // pose already sampled from that history would otherwise steer the
        // dismounted body back towards the saddle for one frame.
        const bool seated = IsSeated();
        if (_wasSeated && !seated) {
            _snapshots.Clear();
            _forcedOffSaddle    = false;
            _jumpCountKnown     = false;
            _riderPressesKnown  = false;
            _jumpPendingUntil   = 0;
            _correctionSteps    = {};
        }
        _wasSeated = seated;

        // Interpolate between bracketing snapshots; use the wire pose until
        // two are available.
        KCDC::Common::Replication::PoseSnapshot pose;
        bool poseHeld              = false;
        const MafiaNet::Time drawn = KCDC::Common::Replication::PoseRenderTime(_snapshots);
        _presentedPoseTime         = _snapshots.Empty() ? 0 : drawn;
        if (!_snapshots.Sample(drawn, pose, &poseHeld)) {
            pose.position        = position;
            pose.velocity        = velocity;
            pose.rotation        = rotation;
            pose.inAir           = inAir;
            pose.input           = input;
            pose.fallSequence    = fallSequence;
            SampleLookPitch(pose);
        }
        if (poseHeld) {
            KCDC::Common::Replication::HoldLocomotion(pose);
        }

        // A jump starts when the drawn pose reaches the one that counted it,
        // which is where the owner left the ground.
        if (!_jumpCountKnown) {
            _jumpCountKnown      = true;
            _appliedJumpCount    = pose.input.jumps;
            _appliedFallSequence = pose.fallSequence;
        }
        else if (pose.input.jumps != _appliedJumpCount) {
            _appliedJumpCount = pose.input.jumps;
            _jumpPendingUntil = MafiaNet::GetTime() + kJumpPendingMs;
        }

        // So does a fall: the body is then drawn where its owner's fell, in
        // the saddle of a horse copy whose stop, rear or throw (`Horse`)
        // played on the same delay. It is struck at the point the owner's
        // body was, taken from where this one stands, and falls from there.
        if (pose.fallSequence != _appliedFallSequence && actor != nullptr) {
            _appliedFallSequence = pose.fallSequence;
            _fallRecoveryUntil   = MafiaNet::GetTime() + kFallRecoveryMs;
            const SDK::Vec3 at   = entity->GetWorldPos();
            KCDC::Features::Player::NativePlayerModule::ReplayFall(actor, at.x + fallHitX, at.y + fallHitY, at.z + fallHitZ);
            Framework::Logging::GetLogger(kLogger)->info("'{}' fell as their owner's body did", nickname);
        }

        // And a knockout. The owner's body went down because its unconscious
        // buff asked for the unconscious action (0x181F9C470), and a copy has
        // no buff to ask; what reaches it is the owner's consciousness, which
        // its soul answers `IsUnconscious` with. So it is asked here, from
        // that, and the ragdoll the action leaves gets up by itself once the
        // owner wakes: its automatic stand-up waits on the same answer
        // (0x180905830), as it did on the owner's machine.
        if (const bool knockedOut = _appliedSoul.valid && _appliedSoul.resources[Shared::Game::HumanSoulState::kHealth].current > 0.0F && _appliedSoul.conditions[Shared::Game::HumanSoulState::kConsciousness] <= 0.0F; knockedOut != _knockedOut && actor != nullptr) {
            _knockedOut                                        = knockedOut;
            SDK::wh::entitymodule::C_ActorStateExpansion *body = actor->GetActionActor()->GetActorStateExpansion();
            if (knockedOut) {
                _fallRecoveryUntil = MafiaNet::GetTime() + kFallRecoveryMs;
                body->RequestUnconsciousAction();
            }
            else {
                body->StopUnconsciousAction();
            }
            Framework::Logging::GetLogger(kLogger)->info("'{}' {} as their owner's body did", nickname, knockedOut ? "was knocked out" : "came to");
        }

        const glm::quat orientation = glm::normalize(pose.rotation);
        if (!KCDC::Common::IsFinite(pose.position)) {
            WithdrawLocomotion();
            return;
        }

        const SDK::Vec3 worldPos {pose.position.x, pose.position.y, pose.position.z};
        const SDK::Quat worldRot {orientation.x, orientation.y, orientation.z, orientation.w};
        // Mounted bodies and native actions own their physical transforms.
        const bool climbing                                          = ApplyLadder(actor, pose.position);
        const bool leaning                                           = ApplyLean(actor);
        SDK::wh::entitymodule::C_ActionActor *actionActor            = actor != nullptr ? actor->GetActionActor() : nullptr;
        SDK::wh::entitymodule::C_ActorStateExpansion *stateExpansion = actionActor != nullptr ? actionActor->GetActorStateExpansion() : nullptr;
        SDK::wh::entitymodule::C_PlayerInput *playerInput            = actor != nullptr ? actor->GetPlayerInput() : nullptr;
        // Crouch is the owner's stance, set as a state: the owner's key is a
        // toggle bit that this body's own PreUpdate would answer with
        // `SetCrouchRequested(!IsCrouchRequested(), false)`
        // (`ApplyMoveStyleToRequest`, 0x1808307D8), so the request is written
        // through that same call, as CNetPlayerInput sets a remote's stance.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (stateExpansion != nullptr && stateExpansion->IsCrouchRequested() != pose.input.crouch) {
            stateExpansion->SetCrouchRequested(pose.input.crouch, false);
        }
        const float frameSeconds = _lastPresentedAt != 0 ? std::min(static_cast<float>(now - _lastPresentedAt) * 0.001F, 0.25F) : 0.0F;
        _lastPresentedAt         = now;

        // A body on a seat, a ladder or a door has no jump to make, and the
        // arc goes by; on foot, ApplyNetworkMovement presses it.
        if (_jumpPendingUntil != 0) {
            if (seated || _wireHorseEntityId != 0 || climbing || leaning || doorInteractionActive || stationOwned || MafiaNet::GetTime() >= _jumpPendingUntil) {
                _jumpPendingUntil = 0;
            }
        }

        // The wire has this body on a horse the engine has not seated it on
        // yet: a mount queued, refused, or played and failed, and asked for
        // again. The owner's pose is the saddle's and its pace the horse's
        // gait, so drawn on foot the body would sprint in the air above the
        // animal. It waits where it stands for the seat instead.
        const bool awaitingSaddle = !seated && _wireHorseEntityId != 0;
        // A station body stands where the station puts it: the dice seat
        // or the forge, else where its owner stands.
        if (stationOwned && !PlaceAtStation(entity)) {
            entity->SetPosRotScale(worldPos, worldRot, SDK::Vec3 {1.0F, 1.0F, 1.0F}, SDK::ENTITY_XFORM_IGNORE_PHYSICS);
        }
        // A fallen body is its ragdoll's to move, as an action's body is the
        // action's: stated a pose meanwhile, it was snapped mid-tumble onto
        // where its owner's tumbled. It gets up apart from its owner, and for
        // a while after closes that gap rather than jumping it.
        const bool fallen = now < _fallRecoveryUntil && actor != nullptr && actor->GetRagdollManager() != nullptr && actor->GetRagdollManager()->IsRagdoll();
        if (fallen) {
            _fallRecoveryUntil = now + kFallRecoveryMs;
        }
        const bool recovering = now < _fallRecoveryUntil;
        glm::vec2 correction(0.0F);
        if (!seated && !awaitingSaddle && !doorInteractionActive && !climbing && !leaning && !sharedAnimation && !actionOwned && !stationOwned && !fallen) {
            const SDK::Vec3 currentPosition = entity->GetWorldPos();
            const glm::vec3 error           = pose.position - glm::vec3(currentPosition.x, currentPosition.y, currentPosition.z);
            // Snap a discontinuity in every axis. Below that threshold,
            // close the horizontal error with a correction velocity
            // that goes into this body's own movement pass (see
            // NativePlayerModule::SubmitCharacterMoveRequest), so physics
            // moves the body there with collisions and ground state intact.
            // Physics controls height. In the air the request carries the
            // jump, and ApplyNetworkMovement steers the body's velocity.
            const glm::vec2 errorXY    = glm::vec2(error.x, error.y);
            const float errorXYLength  = glm::length(errorXY);
            const bool airborne        = pose.inAir || (actor != nullptr && actor->IsJumping());
            const float snapDistanceZ  = airborne ? kSnapDistanceZInAir : kSnapDistanceZ;
            const float snapDistanceXY = recovering ? KCDC::Common::Replication::PoseBufferConfig().warpBase : kSnapDistanceXY;
            const float maxLerpSpeed   = recovering ? kFallRecoveryLerpSpeed : kMaxLerpSpeed;
            if (errorXYLength > snapDistanceXY || std::fabs(error.z) > snapDistanceZ) {
                SDK::IAnimatedCharacter *animated = actor != nullptr ? actor->GetAnimatedCharacter() : nullptr;
                Framework::Logging::GetLogger(kLogger)->debug("PosSnap player={} errorXY={:.2f} errorZ={:.2f} mcmH={} attackActive={} airborne={}", GetNetworkID(), errorXYLength, error.z, animated != nullptr ? static_cast<int>(animated->GetMCMH()) : -1, _remoteAttackActive, airborne);
                entity->SetPos(worldPos, SDK::ENTITY_XFORM_POS, true, true);
            }
            else if (!airborne && frameSeconds > 0.0F) {
                const bool stopped = !pose.input.IsMoving();
                if (!stopped || errorXYLength >= kMinLerpDistance) {
                    const glm::vec2 inFlight  = _correctionSteps[1] + _correctionSteps[2] + _correctionSteps[3];
                    glm::vec2 lerpVelocity    = (errorXY - inFlight) / std::max(kLerpLookAheadSeconds, frameSeconds / kMaxLerpFractionPerFrame);
                    const float lerpSpeed     = glm::length(lerpVelocity);
                    // The correction is forced movement the stride never
                    // sees, so on its feet it is held to what keeps the body
                    // inside the speeds its locomotion animates; a body
                    // getting up keeps the recovery's own limit.
                    const float speedLimit = recovering ? maxLerpSpeed : RemoteCorrectionSpeedLimit(actor, pose.input, errorXYLength, snapDistanceXY, lerpSpeed);
                    if (lerpSpeed > speedLimit) {
                        lerpVelocity *= speedLimit / lerpSpeed;
                    }
                    correction = lerpVelocity * frameSeconds;
                }
            }
            // This body is turned to the yaw the owner's body has. The engine
            // keeps a player body's facing in one place, the actor rotation's
            // base (C_ActorRotation): every ground frame ProcessTurning
            // (0x1809D1D8C) asks the animated character for entity^-1 * base.
            // Writing the base turns the body natively. The turn blend,
            // collisions, a swing's movement and a turn clip's root motion
            // arbitrate it as they do on the owner's machine. The entity is
            // never rotated directly here.
            //
            // The yaw fed is the owner's entity yaw off the pose, not where
            // the owner looks. The look the owner publishes is its final view
            // (C_Player::GetViewRotation), which carries a per-frame offset
            // the base never gets (C_ActorRotation::UpdateViewFinal) and
            // travels at 9 bits against the pose's 12; fed as the base, that
            // offset and that step went into the whole body, and the facing
            // and the axes below were measured against two different things.
            // On the owner's machine the base is rebuilt from the view yaw
            // every frame and the entity closes on it in the same frame, so
            // the entity yaw is the view yaw with nothing trailing to double.
            // Sampled with the pose, so the facing and the input it frames
            // are of one moment.
            const float bodyYaw    = Shared::Entities::HumanEntity::YawOf(orientation);
            const glm::quat facing = Shared::Entities::HumanEntity::RotationForYaw(bodyYaw);
            actor->SetBaseRotation(SDK::Quat {facing.x, facing.y, facing.z, facing.w});
            // The view carries the owner's pitch: a channel actor aims from
            // its movement state's aimDirection, which its movement
            // controller derives from the view (C_Actor::UpdateLookAndAimPose,
            // 0x180640950). After the base: SetViewRotation also rewrites the
            // rotation's angles, which the next UpdateAngles rebuilds the base
            // from, while the base setter leaves them alone under a camera
            // limit. Both end with the base at `facing` either way.
            const glm::quat view = facing * glm::angleAxis(pose.lookPitch, glm::vec3(1.0F, 0.0F, 0.0F));
            actor->SetViewRotation(SDK::Quat {view.x, view.y, view.z, view.w});
            // The combat input listener turns a fighter from its camera: in
            // its free mode it writes the camera yaw into the entity and the
            // view outright, locked on it seeds the turn to the opponent from
            // it. On the owner's machine that camera is the owner's own, so
            // its yaw is the body yaw the owner then has; the copy's camera
            // holds the same yaw and the owner's pitch, one facing throughout.
            KCDC::Features::Player::NativePlayerModule::SetCombatLook(actor, bodyYaw, pose.lookPitch);
        }
        _correctionSteps[0] = correction;
        KCDC::Features::Player::NativePlayerModule::SetMovementCorrection(actor, correction.x, correction.y);

        // In a cart the body's pose is the seat's, in the saddle the horse's,
        // on a bench or a chair the sitting stance's, and leaning out of a
        // window the lean's, but the head is the owner's: turned where they
        // look, as an NPC copy's is.
        const bool riding        = KCDC::Core::gApplication->GetHorseService().SeatOf(_entityId) != 0;
        const bool sittingOnSeat = bodyStance.stance == Shared::Game::BodyStance::Sitting;
        // An owner orbiting their camera publishes a look that leaves their
        // body's facing, which a look out of a body the mouse turns never
        // does beyond its frame's offset: the gap is the signal, so nothing
        // new travels. Wider to start than to keep, so a head at the edge
        // does not flicker, and never with a weapon up, where the fight owns
        // the look.
        constexpr float kLookAsideStart = 0.26F;
        constexpr float kLookAsideKeep  = 0.10F;
        // The look's own yaw: zero faces +Y and a rising yaw swings left, as the pose's does.
        const float lookAsideGap        = headLookReady ? std::atan2(-headLookDirection.x, headLookDirection.y) - Shared::Entities::HumanEntity::YawOf(orientation) : 0.0F;
        const float lookAsideYaw        = std::fabs(std::atan2(std::sin(lookAsideGap), std::cos(lookAsideGap)));
        const bool lookingAside         = !fistsUp && lookAsideYaw > (_lookAside ? kLookAsideKeep : kLookAsideStart);
        _lookAside                      = lookingAside;
        const bool lookOwned            = KCDC::Features::Cart::CartService::Get().IsInCart(_entityId) || riding || sittingOnSeat || leaning || lookingAside;
        if (SDK::IMovementController *movement = (lookOwned || _lookTargetApplied) ? actor->GetMovementController() : nullptr) {
            constexpr float kLookDistance = 10.0F;
            // The owner's look arrives at the pose rate and the look pose
            // blender takes each one at once, so a head followed a quick turn
            // in steps; it eases toward the owner's look instead. A head that
            // starts holding the look takes it as it is.
            constexpr float kLookEaseMs = 150.0F;
            SDK::CMovementRequest request {};
            if (lookOwned && headLookReady) {
                const std::uint64_t now = MafiaNet::GetTime();
                const glm::vec3 eased   = _lookTargetApplied ? glm::mix(_easedLook, headLookDirection, std::clamp(static_cast<float>(now - _easedLookAt) / kLookEaseMs, 0.0F, 1.0F)) : headLookDirection;
                const float length      = glm::length(eased);
                _easedLook              = length > 0.001F ? eased / length : headLookDirection;
                _easedLookAt            = now;
                const SDK::Vec3 at      = entity->GetWorldPos();
                request.SetLookTarget(KCDC::Common::ToNative(glm::vec3(at.x, at.y, at.z + 1.6F) + _easedLook * kLookDistance));
            }
            else {
                request.ClearLookTarget();
            }
            movement->RequestMovement(request);
            _lookTargetApplied = lookOwned && headLookReady;
        }
        // The look target turns the head through the look context, which only
        // does so while a looking clip asks for it too
        // (CProceduralContextLook::Update, 0x180C4EF54); the lean's and the
        // seats' fragments carry none, so while they last -- and in the
        // saddle -- the context is told one does as it updates
        // (NativePlayerModule::SetLookHeld). On the frame they end the
        // request is put down once, as a leaving clip does.
        const bool clipLook = riding || sittingOnSeat || leaning || lookingAside;
        if (clipLook || _lookClipRequested) {
            KCDC::Features::Player::NativePlayerModule::SetLookHeld(_entityId, clipLook);
            SDK::CAnimatedCharacter *animated  = clipLook ? nullptr : actor->GetAnimatedCharacterRuntime();
            SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
            if (auto *look = controller != nullptr ? static_cast<SDK::CProceduralContextLook *>(controller->FindProceduralContext("ProceduralContextLook")) : nullptr) {
                look->SetClipRequestsLooking(false);
            }
            _lookClipRequested = clipLook;
        }

        // In the saddle the same replay drives the horse: this body's
        // PreUpdate hands the stick to its C_RiderPlayerInput, which steers
        // the horse copy as the owner's steers theirs (see Horse).
        if (riding && !doorInteractionActive && !sharedAnimation && !actionOwned && _appliedSoul.CanAct()) {
            ApplyNetworkMovement(pose, true);
            return;
        }

        // Feed the native player controller after interpolating the pose.
        if (seated || awaitingSaddle || doorInteractionActive || climbing || leaning || sharedAnimation || actionOwned || stationOwned || !_appliedSoul.CanAct()) {
            WithdrawLocomotion();
            return;
        }

        ApplyNetworkMovement(pose);
        CaptureLocomotionTags();
    }

    void Human::RequestSpawn() {
        // Transform and appearance readiness arrive on separate channels.
        // One request in flight at a time; the spawner calls back when it runs.
        if (_spawnRefused || _spawnTicket != nullptr || !transformReady || !soul.valid || poseIncarnation != soul.incarnation) {
            return;
        }

        const auto now = MafiaNet::GetTime();
        if (now < _nextSpawnAttempt) {
            return;
        }
        _nextSpawnAttempt = now + KCDC::Common::Spawn::SpawnRetryDelayMs(GetNetworkID());
        if (SDK::wh::rpgmodule::I_SoulSystem::GetInstance() == nullptr) {
            return;
        }

        // A real C_Player on a channel of its own, the way CryEngine's actor
        // system spawns a remote player; the native player module keeps its
        // local-player work off this machine's player.
        const SDK::ChannelId channelId = KCDC::Features::Player::NativePlayerModule::Get().AllocateChannelId();
        if (channelId == 0) {
            Framework::Logging::GetLogger(kLogger)->error("No channel id left for '{}' (networkId {})", nickname, GetNetworkID());
            return;
        }
        KCDC::Common::Spawn::SpawnRequest request;
        request.channelId     = channelId;
        request.actorClass    = SDK::ActorClassNames::Player;
        request.soulArchetype = appearance.gender == Shared::Game::AppearanceGender::Female ? "NPC_Female" : "";
        for (const BodyProperty &property : BodyProperties(appearance.gender)) {
            request.properties.emplace_back(property.key, std::string(property.value));
        }
        // No bIsDummy: the body registers with the NPC manager as Player.lua
        // registers every player, through the player branch of RegisterNPC
        // (0x180417DF0) -- a C_NPC record with the blank soul's empty brain,
        // perception and the carry and stealth links. The branch's writes to
        // the local player's cache are put back by NativePlayerModule.
        _bodyGender = appearance.gender;
        // A blank soul leaves clothing under replicated inventory control.
        request.world = KCDC::Common::Spawn::WorldPlacement {SDK::Vec3 {position.x, position.y, position.z}, SDK::Quat {rotation.x, rotation.y, rotation.z, rotation.w}};

        _spawnTicket = KCDC::Core::gApplication->GetActorSpawner().Request(request, [this](KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
            OnSpawned(ticket, spawned);
        });
    }

    void Human::OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(true);
        // The ticket has run either way, so it never fires again. Cleared
        // before anything below, because the rollback path calls Despawn, which
        // would otherwise try to return a ticket that is mid-callback.
        _spawnTicket = nullptr;

        if (!spawned) {
            if (KCDC::Common::Spawn::IsRetryableSpawnResult(ticket.GetResult())) {
                return;
            }

            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Could not spawn a body for '{}' (networkId {}): {}", nickname, GetNetworkID(), KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
            return;
        }

        {
            _entityId = ticket.GetEntityId();
            BindNativePlayer();

            const LookResult look = ApplyLook();

            if (look != LookResult::Applied) {
                // A body the engine refused is refused for good; one that
                // merely had nothing to bind to yet is worth another second.
                const bool retryable = look == LookResult::Retry;
                Despawn();
                _spawnRefused = !retryable;
                if (retryable) {
                    Framework::Logging::GetLogger(kLogger)->debug("Body for '{}' (networkId {}) was not ready to be souled; retrying", nickname, GetNetworkID());
                }
                else {
                    Framework::Logging::GetLogger(kLogger)->error("Rolled back the incomplete body for '{}' (networkId {})", nickname, GetNetworkID());
                }
                return;
            }

            _nextSpawnAttempt = 0;

            Framework::Logging::GetLogger(kLogger)->info("Spawned a body for '{}' (networkId {}): entity {}", nickname, GetNetworkID(), _entityId);
        }
    }

    void Human::ReleaseSpawnTicket() {
        if (_spawnTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().Return(_spawnTicket);
        }
        _spawnTicket = nullptr;
    }

    void Human::RefreshAnimationBinding(SDK::wh::entitymodule::C_Actor *actor) {
        auto *entity = actor->GetEntity();
        if (entity == nullptr) {
            return;
        }

        auto *character         = entity->GetCharacter(0);
        auto *animatedCharacter = actor->GetAnimatedCharacterRuntime();

        if (character == nullptr || character == _boundCharacter || animatedCharacter == nullptr) {
            return;
        }

        animatedCharacter->RefreshCharacterBindings();
        _boundCharacter = character;
    }

    Human::LookResult Human::ApplyLook() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(_entityId) : nullptr;
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Entity {} disappeared during body setup", _entityId);
            return LookResult::Refused;
        }

        SDK::wh::entitymodule::C_Player *actor = GetPlayer();
        if (actor == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("No actor for entity {}", _entityId);
            return LookResult::Refused;
        }

        auto *soulSystem = SDK::wh::rpgmodule::I_SoulSystem::GetInstance();
        if (soulSystem == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("No soul system for entity {}", _entityId);
            return LookResult::Refused;
        }

        // This accessor ensures soul initialization, unlike the cached pointer.
        auto *soul = SDK::wh::rpgmodule::GetOrCreateSoul(soulSystem, entity);
        if (soul == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Entity {} has no initialized soul", _entityId);
            return LookResult::Refused;
        }

        // Bind the soul before dressing queries it. Missing game objects or
        // snapshots are retryable.
        if (!ApplySoulState(actor)) {
            return LookResult::Retry;
        }

        actor->RefreshClothingSupport();

        // Wake before creating the equipment listener. RequestPutAway keeps
        // the wanted/applied bits and pending-list membership consistent.
        // The wake is also what loads the character, through the engine's
        // own SetupActorModel.
        actor->RequestPutAway(false, true);
        if (!KCDC::Common::Spawn::ActorSpawner::EnsureClassCharacter(entity)) {
            Framework::Logging::GetLogger(kLogger)->error("Entity {} loaded no character model", _entityId);
            return LookResult::Refused;
        }
        ApplyGenderTag(actor, _bodyGender);

        actor->EnsureEquipmentHandler();
        actor->RebuildClothing();

        // Native player spawning completes with Revive, which initializes the
        // actor state hierarchy. Construction alone leaves no Idle state for
        // mount and jump admission. The remote scope protects local globals.
        actor->Revive(SDK::wh::entitymodule::C_Actor::eReviveReason_GameModeInit);

        // ApplyAppearance goes after MakeLookAsSoul rather than instead of
        // it: that native also pairs the equipment listener with the source
        // actor, and what it writes is the very layer this overwrites.
        //
        // Against actor->GetSoul() rather than the soul resolved above,
        // because that is where MakeLookAsSoul writes (C_Actor +0x668) and
        // what PresentRemoteState re-applies to. They are the same object
        // for a blank copy, and this does not depend on it.
        actor->MakeLookAsSoul(soul);
        ApplyAppearance(actor->GetSoul());
        ApplyOutfit(actor);

        // Dressing can replace the character instance.
        RefreshAnimationBinding(actor);
        return LookResult::Applied;
    }

    void Human::ApplyOutfit(SDK::wh::entitymodule::C_Actor *actor) {
        // The reconciliation itself is shared with every other replicated body
        // that wears what the wire says -- see common/actor_dressing.h.
        // What stays here is this feature's own bookkeeping: a body that was not
        // ready keeps its dirty flag and is dressed on a later frame.
        constexpr MafiaNet::Time kWeaponRetryMs = 250;

        // A retry only re-checks deferred weapons; its failures were already reported.
        const bool retry = !_equipmentDirty && _weaponsPending;
        KCDC::Common::DressOptions options;
        options.reconcileWeaponSlots = true;
        options.deferWeaponChange    = [this](const SDK::wh::entitymodule::S_ItemClassGUID &itemClass) {
            const Shared::RPC::ItemClassId weapon {itemClass.m_low, itemClass.m_high};
            for (std::size_t hand = 0; hand < Shared::Entities::HumanEntity::kHandCount; ++hand) {
                if (weapon == handItemClass[hand] || weapon == _shadowHandClass[hand]) {
                    return true;
                }
            }
            return KCDC::Features::GroundItem::GroundItemService::HasPendingHeldItemDrop(GetNetworkID(), weapon);
        };
        const KCDC::Common::DressResult result = KCDC::Common::ApplyActorEquipment(actor, equipment, options);
        _weaponRetryAt                          = MafiaNet::GetTime() + kWeaponRetryMs;
        if (!result.ready) {
            if (!retry) {
                Framework::Logging::GetLogger(kLogger)->error("Entity {} was not ready to be dressed; the body stays bare for now", _entityId);
            }
            return;
        }
        if (!result.dressed && !retry) {
            Framework::Logging::GetLogger(kLogger)->error("Entity {} could not be dressed in its {} piece(s); it keeps what it had on", _entityId, result.armorPieces);
        }
        _equipmentDirty = false;
        _weaponsPending = result.pendingWeapons;
        _wearDirty      = _wearDirty || result.changed;
    }

    // --- Combat --------------------------------------------------------------

    void Human::RecordCombatSample() {
        if (_combatTimelineCount != 0) {
            const CombatSample &newest = _combatTimeline[(_combatTimelineHead + _combatTimelineCount - 1) % kCombatTimelineCapacity];
            if (newest.fistsUp == fistsUp && newest.guardRequest == guardRequest && newest.combatZone == combatZone) {
                return;
            }
        }
        // Full: the oldest is dropped, which only hurries a backlog along.
        if (_combatTimelineCount == kCombatTimelineCapacity) {
            _combatTimelineHead = (_combatTimelineHead + 1) % kCombatTimelineCapacity;
            --_combatTimelineCount;
        }
        _combatTimeline[(_combatTimelineHead + _combatTimelineCount) % kCombatTimelineCapacity] = {lastUpdateTime, fistsUp, guardRequest, combatZone};
        ++_combatTimelineCount;
    }

    Human::CombatSample Human::DrawnCombat() {
        if (_combatTimelineCount == 0) {
            return {lastUpdateTime, fistsUp, guardRequest, combatZone};
        }
        const Human *opponent        = combatTargetNetworkId != 0 ? KCDC::Common::FindReplicaById<Human>(combatTargetNetworkId) : nullptr;
        const bool atLocalBody       = opponent != nullptr && opponent->IsOwner();
        const MafiaNet::Time drawnAt = atLocalBody || _presentedPoseTime == 0 ? std::numeric_limits<MafiaNet::Time>::max() : _presentedPoseTime;
        while (_combatTimelineCount > 1 && _combatTimeline[(_combatTimelineHead + 1) % kCombatTimelineCapacity].at <= drawnAt) {
            const std::size_t next = (_combatTimelineHead + 1) % kCombatTimelineCapacity;
            // Every press is owed, not just the newest state: several samples
            // drawn in one frame -- a release delivered with its press -- would
            // otherwise never show the guard.
            if (_combatTimeline[_combatTimelineHead].guardRequest == 0 && _combatTimeline[next].guardRequest != 0 && _owedGuardPresses < kCombatTimelineCapacity) {
                if (_owedGuardPresses == 0) {
                    _guardPressOwedAt = MafiaNet::GetTime();
                }
                ++_owedGuardPresses;
            }
            _combatTimelineHead = next;
            --_combatTimelineCount;
        }
        return _combatTimeline[_combatTimelineHead];
    }

    bool Human::ApplyCombat(SDK::wh::entitymodule::C_Actor *actor, bool suppress) {
        const CombatSample drawn = DrawnCombat();
        const bool fighting      = !suppress && drawn.fistsUp && _appliedSoul.CanAct();
        if (actor == nullptr) {
            return false;
        }
        if (!suppress) {
            _combatPresentationSuppressed = false;
        }
        // A body whose owner has its fists down is left alone -- unless its
        // own fists are still up, which needs the same exit the owner took.
        if (!fighting && !_stanceRequested && !suppress) {
            SDK::wh::combatmodule::C_CombatActor *existing = actor->GetCombatActor();
            if (existing == nullptr || !existing->IsInitialised() || !existing->IsFightingStanceUp()) {
                return true;
            }
        }

        // Built on demand: a body that has never fought has none.
        auto *combatActor = fighting ? actor->GetOrCreateCombatActor() : actor->GetCombatActor();
        if (combatActor == nullptr) {
            _combatPresentationSuppressed = suppress;
            return !fighting;
        }
        auto *model = combatActor->GetModel();
        if (model == nullptr || !combatActor->IsInitialised()) {
            return false;
        }

        if (!fighting && _remoteAttackActive) {
            CancelRemoteAttack(combatActor);
        }

        // A player's fighter is a C_CombatPlayer, and its controller is the
        // object the owner's block key drove.
        SDK::wh::combatmodule::C_CombatPlayerController *controller = static_cast<SDK::wh::combatmodule::C_CombatPlayer *>(combatActor)->GetController();
        // This body's own press of the block key: the guard its controller
        // asked for as the player (BeginGuard's `RequestGuard(1, 0)`), and
        // whether its release is already counting down. Not the block
        // request bit, which BeginGuard sets only for a fighter without an
        // opponent (0x1827C29E0): a copy that pressed while locked on read
        // as never pressed, was never released, and stood in
        // PreparingToParry for as long as its owner stood in a plain guard,
        // its own block trigger meeting the next swing.
        const bool guardHeld      = model->GetGuardRequest().GetSource(SDK::wh::combatmodule::C_CombatActor::GUARD_SOURCE_PLAYER) != SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE;
        const bool guardReleasing = controller != nullptr && controller->IsGuardReleasePending();

        if (suppress) {
            if (!_combatPresentationSuppressed) {
                CancelRemoteAttack(combatActor);
                combatActor->LeaveCombatModeIfActive();
            }
            if (controller != nullptr && guardHeld) {
                controller->EndGuard();
            }
            // The interaction needs the arms down now, not on EndGuard's
            // release delay.
            combatActor->RequestGuard(SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE, SDK::wh::combatmodule::C_CombatActor::GUARD_SOURCE_PLAYER);

            if (!_combatPresentationSuppressed) {
                if (!_stanceRequested && !combatActor->IsFightingStanceUp()) {
                    _combatPresentationSuppressed = true;
                }
                else {
                    const bool accepted = combatActor->RequestFightingStance(false);
                    if (accepted) {
                        _stanceRequested              = false;
                        _combatPresentationSuppressed = true;
                    }
                }
            }
            const bool ready = _combatPresentationSuppressed && !combatActor->IsFightingStanceUp();
            return ready;
        }

        // The owner's fists come down when its engine leaves combat mode
        // (LeaveCombatMode, which is the edge the wire reports), and this body
        // cannot follow by stance request alone: in combat mode its
        // C_ActorActionCombat outranks a stance change in the actor queue,
        // and the mode-lock request bit (model+0xB58 element 0) that keeps it
        // there stays set on this body after the owner's has cleared. So the
        // owner's exit is replayed -- the guarded native one -- and the stance
        // asked down after it, paced until it lands.
        if (!fighting && combatActor->IsFightingStanceUp()) {
            const MafiaNet::Time now = MafiaNet::GetTime();
            if (now >= _nextStanceRequestAt) {
                combatActor->LeaveCombatModeIfActive();
                combatActor->RequestFightingStance(false);
                _stanceRequested     = false;
                _nextStanceRequestAt = now + kInputRetryMs;
            }
        }
        // Asked once per wire edge; NotifyCombatStanceLost clears the latch
        // when the engine takes the fists back down.
        else if (fighting != _stanceRequested) {
            if (combatActor->RequestFightingStance(fighting)) {
                _stanceRequested = fighting;
            }
        }

        if (!fighting) {
            _owedGuardPresses = 0;
            if (controller != nullptr && guardHeld && !guardReleasing) {
                controller->EndGuard();
            }
            return true;
        }

        // The owner's direction on the combat star, with the guard side it
        // needs: `TryApplyCombatZone` (0x180936A28), what the owner's own
        // input runs in the guard and while the guard reads to strike
        // (`RequestCombatZone`), keeps the current side if the weapon's
        // swings reach the zone from it, else turns to the other, and sets
        // the side with the zone. Set plainly, the zone moved under a guard
        // that kept its side: a copy that took its owner's zone while
        // reading to strike, recovering from a hit or withdrawing showed the
        // guard of the side before. Where the owner's input would have
        // applied it, a change the running action refuses is asked again
        // next frame; elsewhere that input only stores the zone as pending,
        // while the guard action reads the current zone as it is built, so
        // a refused one is set plainly. So is one refused to a copy without
        // an opponent: the swings the side is picked by are queried against
        // the opponent, and a guard holding none took none of its owner's
        // zones for seconds, though its owner was locked on.
        if (drawn.combatZone >= 0 && model->GetCombatZone() != drawn.combatZone) {
            combatActor->TryApplyCombatZone(drawn.combatZone, model->GetGuardSide());
            const bool inputTakesZone = model->GetState() == SDK::wh::combatmodule::E_CombatActorStateId::Guard || model->IsGuardReadingToStrike();
            if (model->GetCombatZone() != drawn.combatZone && (!inputTakesZone || combatActor->GetOpponent() == nullptr)) {
                combatActor->SetCombatZone(drawn.combatZone);
            }
        }

        // The owner's block key, pressed and released on this body's own
        // controller: BeginGuard builds the guard, EndGuard lets it down on
        // the engine's release delay -- once, as each call starts that delay
        // over. A press this body refuses -- it is still finishing what the
        // owner already has -- is pressed again, paced, until it takes, as a
        // held key would be; a press during the delay takes the guard back.
        // Each press the owner made is pressed here (`_owedGuardPresses`),
        // and released before the next one, even where its release was drawn
        // before it took: the guard then shows for the release delay, as on
        // the owner's screen.
        if (controller != nullptr) {
            const MafiaNet::Time now = MafiaNet::GetTime();
            if (_owedGuardPresses != 0 && now - _guardPressOwedAt > kGuardPressBudgetMs) {
                _owedGuardPresses = 0;
            }
            if ((drawn.guardRequest != 0 || _owedGuardPresses != 0) && (!guardHeld || guardReleasing) && now >= _nextGuardPressAt) {
                controller->BeginGuard();
                _nextGuardPressAt = now + kInputRetryMs;
                // Taken when it asks the player's guard with no release
                // pending -- what the owner's own press published.
                if (_owedGuardPresses != 0 && model->GetGuardRequest().GetSource(SDK::wh::combatmodule::C_CombatActor::GUARD_SOURCE_PLAYER) != SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE && !controller->IsGuardReleasePending()) {
                    --_owedGuardPresses;
                    _guardPressOwedAt = now;
                }
            }
            else if (guardHeld && !guardReleasing && drawn.guardRequest != 0) {
                // The guard the owner holds now shows every press it made.
                _owedGuardPresses = 0;
            }
            else if (guardHeld && !guardReleasing) {
                controller->EndGuard();
            }
        }
        if (_remoteAttackActive && _remoteAttackInputClass >= 0 && (!model->IsAttackInputActive() || model->GetAttackInputClass() != _remoteAttackInputClass)) {
            combatActor->BeginAttack(_remoteAttackInputClass);
        }
        return true;
    }

    Human::RemoteAttackResult Human::PerformRemoteAttack(const Shared::RPC::CombatAttack &attack) {
        if (!HasAppliedSoulState()) {
            return RemoteAttackResult::Deferred;
        }
        if (!_appliedSoul.CanAct()) {
            return RemoteAttackResult::Obsolete;
        }

        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();
        if (actor == nullptr) {
            return RemoteAttackResult::Deferred;
        }
        SDK::wh::combatmodule::C_CombatActor *combatActor = actor->GetOrCreateCombatActor();
        SDK::wh::combatmodule::C_CombatActorModel *model  = combatActor != nullptr ? combatActor->GetModel() : nullptr;
        if (combatActor == nullptr || !combatActor->IsInitialised() || model == nullptr || combatActor->GetActionManager() == nullptr || combatActor->GetActionManager()->GetAttackFactory() == nullptr) {
            return RemoteAttackResult::Deferred;
        }

        if (attack.phase == Shared::RPC::CombatAttack::Cancel) {
            if (_remoteAttackGeneration != 0 && _remoteAttackGeneration != attack.attackGeneration) {
                return RemoteAttackResult::Obsolete;
            }
            CancelRemoteAttack(combatActor);
            return RemoteAttackResult::Applied;
        }

        if (attack.phase == Shared::RPC::CombatAttack::Press) {
            if (_remoteAttackGeneration == attack.attackGeneration && _remoteAttackActive) {
                return RemoteAttackResult::Applied;
            }
            if (_remoteAttackGeneration != 0) {
                return RemoteAttackResult::Obsolete;
            }

            if (!combatActor->IsFightingStanceUp() && !_stanceRequested) {
                if (combatActor->RequestFightingStance(true)) {
                    _stanceRequested = true;
                }
                else {
                    return RemoteAttackResult::Deferred;
                }
            }
            KCDC::Core::gApplication->GetPlayerDuelService().SquareUp(combatActor, attack.opponentNetworkId);
            // The zone the owner's press swung from, after the lock it swung
            // under and with the guard side it needs; refused, it is set
            // plainly, as the swing is built from it.
            if (attack.zoneId >= 0 && model->GetCombatZone() != attack.zoneId) {
                combatActor->TryApplyCombatZone(attack.zoneId, model->GetGuardSide());
                if (model->GetCombatZone() != attack.zoneId) {
                    combatActor->SetCombatZone(attack.zoneId);
                }
            }

            combatActor->BeginAttack(attack.inputClassId);
            if (!model->IsAttackInputActive() || model->GetAttackInputClass() != attack.inputClassId) {
                return RemoteAttackResult::Deferred;
            }

            _remoteAttackInputClass = attack.inputClassId;
            _remoteAttackGeneration = attack.attackGeneration;
            _remoteAttackActive     = true;
            return RemoteAttackResult::Applied;
        }

        if (_remoteAttackGeneration != 0 && _remoteAttackGeneration != attack.attackGeneration) {
            return RemoteAttackResult::Obsolete;
        }
        if (SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRow(attack.attackRowIndex) == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Rejected replicated attack Commit with an absent native row: player={}, generation={}, row={}", GetNetworkID(), attack.attackGeneration, attack.attackRowIndex);
            CancelRemoteAttack(combatActor);
            return RemoteAttackResult::Obsolete;
        }
        if (attack.zoneId >= 0) {
            combatActor->SetCombatZone(attack.zoneId);
        }

        // The timeline runs at the sender's speed rather than this engine's
        // own answer, which would differ: it depends on who the swinger is
        // squared up against.
        PlayerDuelService &duel = KCDC::Core::gApplication->GetPlayerDuelService();
        duel.SquareUp(combatActor, attack.opponentNetworkId);
        duel.ForceNextAttackSpeed(combatActor, attack.attackSpeed);

        const SDK::wh::combatmodule::C_CombatActor::AttackOutcome outcome = combatActor->ReplayAcceptedAttack(attack.attackRowIndex, attack.candidateFlag0, attack.candidateFlag1, attack.contextFlag0, attack.contextFlag1, attack.stateMode);
        if (outcome != SDK::wh::combatmodule::C_CombatActor::AttackOutcome::Swung) {
            duel.ForgetForcedAttackSpeed(combatActor);
        }
        if (outcome == SDK::wh::combatmodule::C_CombatActor::AttackOutcome::Swung) {
            // A blow this swing lands on the local body is reported to the
            // referee under the generation its owner gave it.
            duel.NoteReplayedSwing(combatActor, attack.attackGeneration, attack.windowMs);
            CancelRemoteAttack(combatActor);
            return RemoteAttackResult::Applied;
        }
        if (outcome == SDK::wh::combatmodule::C_CombatActor::AttackOutcome::InvalidRow) {
            CancelRemoteAttack(combatActor);
            return RemoteAttackResult::Obsolete;
        }

        return RemoteAttackResult::Deferred;
    }

    void Human::CancelRemoteAttackGeneration(std::uint32_t attackGeneration) {
        if (attackGeneration == 0 || attackGeneration != _remoteAttackGeneration) {
            return;
        }
        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();
        CancelRemoteAttack(actor != nullptr ? actor->GetCombatActor() : nullptr);
    }

    void Human::CancelRemoteAttack(SDK::wh::combatmodule::C_CombatActor *combatActor) {
        // The owner's AbortAttack, on this body's own controller: it also
        // resets the controller's press state, which clearing the input class
        // and the request bit by hand left behind.
        SDK::wh::combatmodule::C_CombatPlayerController *controller = combatActor != nullptr && combatActor->IsInitialised() ? static_cast<SDK::wh::combatmodule::C_CombatPlayer *>(combatActor)->GetController() : nullptr;
        if (controller != nullptr) {
            controller->AbortAttack();
        }
        _remoteAttackInputClass = -1;
        _remoteAttackGeneration = 0;
        _remoteAttackActive     = false;
    }

    void Human::CancelRemoteCombatAttack() {
        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();
        CancelRemoteAttack(actor != nullptr ? actor->GetCombatActor() : nullptr);
    }

    // --- Ledges --------------------------------------------------------------

    bool Human::PerformRemoteLedgeGrab(bool jumpRequested, bool crouchRequested) {
        if (!HasAppliedSoulState() || !_appliedSoul.CanAct()) {
            return false;
        }

        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();
        if (actor == nullptr) {
            return false;
        }

        // Run the native vault query at the replicated pose, asked with the
        // request the owner's vault was asked with.
        return actor->TryLedgeGrab(SDK::wh::entitymodule::S_LedgeGrabInput {jumpRequested, crouchRequested});
    }

    // --- Ladders -------------------------------------------------------------

    bool Human::ApplyLadder(SDK::wh::entitymodule::C_Actor *actor, const glm::vec3 &position) {
        // IsSettled for the reason the hand items wait on it: the paths below
        // reach the actor model, and a half-built body has none.
        if (actor == nullptr || !actor->IsSettled()) {
            return false;
        }

        SDK::wh::entitymodule::C_ActorActionLadder *running = KCDC::Core::gApplication->GetPlayerService().LadderActionFor(_entityId);
        if (running == nullptr) {
            // Nothing to leave, so nothing is owed a get-off.
            _ladderExitRequested = false;
        }
        if (ladderGuid != _ladderGrabGuid) {
            // A new intent supersedes whatever was in flight for the old one.
            _ladderGrabGuid     = ladderGuid;
            _ladderGrabAttempts = 0;
            _ladderGrabAt       = 0;
        }

        SDK::wh::entitymodule::C_ActionActor *queue                  = actor->GetActionActor();
        SDK::wh::entitymodule::C_ActorStateExpansion *stateExpansion = queue != nullptr ? queue->GetActorStateExpansion() : nullptr;
        if (stateExpansion == nullptr) {
            return running != nullptr;
        }

        if (ladderGuid == 0) {
            if (running == nullptr) {
                return false;
            }
            if (!_ladderExitRequested) {
                _ladderExitRequested = true;

                // The native ladder exit selects the nearer end and plays its
                // get-off animation.
                stateExpansion->SetLadderClimbInput(0.0F);
                stateExpansion->ExitLadderNearPosition(KCDC::Common::ToNative(position));
            }

            // Still climbing until the get-off finishes and the action
            // exits, which is what clears the register this reads.
            return true;
        }

        auto *entitySystem                 = SDK::IEntitySystem::GetInstance();
        const SDK::EntityId ladderEntityId = entitySystem != nullptr ? entitySystem->FindEntityByGuid(ladderGuid) : 0;
        if (ladderEntityId == 0) {
            // The ladder is in the level but not streamed in here yet. The
            // body keeps whatever it is doing until it is.
            return running != nullptr;
        }

        if (running != nullptr && running->GetLadderEntityId() == ladderEntityId) {
            _ladderGrabAt = 0;
            stateExpansion->SetLadderClimbInput(ladderClimbInput);
            return true;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_ladderGrabAt != 0 && now - _ladderGrabAt < kLadderActionTimeoutMs) {
            // A grab is queued and has not entered yet.
            return true;
        }
        if (_ladderGrabAttempts >= kMaxLadderGrabAttempts) {
            return running != nullptr;
        }

        ++_ladderGrabAttempts;
        if (!stateExpansion->RequestLadderAction(ladderEntityId)) {
            if (_ladderGrabAttempts == kMaxLadderGrabAttempts) {
                Framework::Logging::GetLogger(kLogger)->warn("Gave up putting human {} on ladder {:#x} after {} refused grabs", GetNetworkID(), ladderGuid, kMaxLadderGrabAttempts);
            }
            return running != nullptr;
        }

        _ladderGrabAt = now;

        // The register is filled in by the entry hook, which has not run yet.
        // Stating the input now blends the get-on into a climb, not a hold.
        stateExpansion->SetLadderClimbInput(ladderClimbInput);
        return true;
    }

    // --- Leaning out ---------------------------------------------------------

    bool Human::ApplyLean(SDK::wh::entitymodule::C_Actor *actor) {
        if (actor == nullptr || !actor->IsSettled()) {
            return false;
        }

        if (battlementGuid == 0) {
            _leanStartGuid = 0;
            _leanStartSlot = 0;
        }
        // Only ApplyLean starts a copy's lean, so a copy it never started has
        // none to look up.
        if (battlementGuid == 0 && !_leanStarted) {
            return false;
        }

        auto *lean         = SDK::wh::playermodule::C_BattleArchery::Of(actor, false);
        const bool leaning = lean != nullptr && lean->IsActive();
        _leanStarted       = leaning;

        // The slot the owner leans out of, when its battlement is streamed in
        // here; null before it is.
        auto *entitySystem                              = battlementGuid != 0 ? SDK::IEntitySystem::GetInstance() : nullptr;
        auto *battlementEntity                          = entitySystem != nullptr ? entitySystem->GetEntityByGuid(battlementGuid) : nullptr;
        auto *battlement                                = SDK::wh::playermodule::C_Battlement::Find(battlementEntity);
        SDK::wh::playermodule::C_BattlementSlot *wanted = battlement != nullptr ? battlement->GetSlot(battlementSlot) : nullptr;

        if (leaning) {
            // Out the way the owner's exit key took the owner: WallLookOut
            // and the slide back, then Finish. Still leaning until then. Also
            // when the owner now leans out of another slot, whose lean starts
            // once this one has finished. Any state in the slot, not only the
            // lean: the copy's own bow, drawn by the hand-item sync, turns the
            // same minigame into archery (state 2), and a copy left in it
            // never got out -- it slid about with its locomotion off for good.
            const bool ownerLeft = battlementGuid == 0 || (wanted != nullptr && wanted != lean->GetSlot());
            if (ownerLeft && lean->IsInSlot()) {
                lean->BeginExit();
            }
            return true;
        }

        // One start per lean: a refused one, or one the engine itself ended
        // early, is not asked for again.
        if (battlementGuid == 0 || (battlementGuid == _leanStartGuid && battlementSlot == _leanStartSlot)) {
            return false;
        }
        if (battlementEntity == nullptr) {
            // Not streamed in here yet. The body keeps whatever it is doing
            // until it is.
            return false;
        }
        _leanStartGuid = battlementGuid;
        _leanStartSlot = battlementSlot;

        // What C_BattlementManager::StartBattleArcheryAtSlot does with the
        // slot it picked: the actor's minigame, found or made, started on it.
        // The engine then plays WallLookIn and the WallLook loop and slides
        // the body onto the slot, as it did the owner's.
        SDK::wh::playermodule::C_BattlementSlot *slot = battlement != nullptr ? battlement->GetUnoccupiedSlot(battlementSlot) : nullptr;
        if (slot == nullptr || SDK::wh::playermodule::C_BattleArchery::Of(actor, true)->Start(slot, 0) != 0) {
            Framework::Logging::GetLogger(kLogger)->warn("Human {} could not lean out of battlement {:#x} slot {}", GetNetworkID(), battlementGuid, battlementSlot);
            return false;
        }
        _leanStarted = true;
        return true;
    }

    // --- Held items ----------------------------------------------------------

    SDK::wh::entitymodule::C_Item *Human::ReleaseHeldShadowForGroundDrop(const Shared::RPC::ItemClassId &droppedClass, bool retiresHeldItem) {
        // Consume the shadow only when the held item is retired; partial
        // drops keep it.
        const std::size_t hand = retiresHeldItem && !IsOwner() ? FindHandShadow(droppedClass) : Shared::Entities::HumanEntity::kHandCount;
        if (hand == Shared::Entities::HumanEntity::kHandCount) {
            return nullptr;
        }

        auto *human     = static_cast<SDK::wh::entitymodule::C_Human *>(ResolveActor());
        auto *runtime   = human != nullptr ? human->GetInventorySoulRuntime() : nullptr;
        auto *inventory = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetInventory() : nullptr;
        auto *held      = FindInventoryItemByWuid(inventory, _shadowHandWuid[hand]);
        if (held == nullptr) {
            return nullptr;
        }

        SDK::wh::entitymodule::C_Item *dropped = nullptr;
        if (!human->InvokeDropInventoryItem(_shadowHandWuid[hand], held->GetAmount(), 0, &dropped) || dropped == nullptr) {
            return nullptr;
        }

        // The ground replica can arrive before the owner's empty-hand update.
        // Retain the consumed class until that update so the old held state
        // cannot materialize and draw another copy of the dropped weapon.
        _shadowHandWuid[hand]     = 0;
        _shadowRetiredAt[hand]    = MafiaNet::GetTime();
        _appliedHandWuid[hand]    = 0;
        _handCreateAttempts[hand] = 0;
        _remoteHandStage[hand]    = static_cast<std::uint8_t>(HandItemStage::Holstered);
        return dropped;
    }

    void Human::ApplyHandItems(SDK::wh::entitymodule::C_Human *human) {
        ApplyHandItem(human, Shared::Entities::HumanEntity::kRightHand);
        ApplyHandItem(human, Shared::Entities::HumanEntity::kLeftHand);
    }

    void Human::ApplyHandItem(SDK::wh::entitymodule::C_Human *human, std::size_t hand) {
        constexpr int kMaxCreateAttempts             = 3;
        constexpr MafiaNet::Time kRetiredShadowHoldMs = 2000;

        const auto setStage = [&](HandItemStage stage) {
            const std::uint8_t encodedStage = static_cast<std::uint8_t>(stage);
            if (_remoteHandStage[hand] == encodedStage) {
                return false;
            }
            _remoteHandStage[hand] = encodedStage;
            return true;
        };

        if (human == nullptr) {
            return;
        }

        // Most hands hold nothing, permanently.
        if (!handItemClass[hand].IsValid() && _shadowHandWuid[hand] == 0) {
            _shadowHandClass[hand] = {};
            setStage(HandItemStage::Holstered);
            return;
        }

        if (_shadowHandWuid[hand] == 0 && handItemClass[hand] == _shadowHandClass[hand]) {
            if (MafiaNet::GetTime() - _shadowRetiredAt[hand] < kRetiredShadowHoldMs) {
                setStage(HandItemStage::TransitionPending);
                return;
            }
            // The owner drew the same class again before its empty hand was
            // replicated; this is a new item, not the dropped one.
            _shadowHandClass[hand] = {};
        }

        auto *attachmentManager = human->GetItemAttachmentManager();
        if (attachmentManager == nullptr) {
            if (setStage(HandItemStage::MissingItemAttachmentManager)) {
                Framework::Logging::GetLogger(kLogger)->error("Remote player {} has no item attachment manager for hand {} presentation", GetNetworkID(), hand);
            }
            return;
        }

        auto *handHelper = human->GetHumanHandHelper();
        if (handHelper == nullptr || handHelper->GetHandInterface() == nullptr) {
            if (setStage(HandItemStage::MissingHandHelper)) {
                Framework::Logging::GetLogger(kLogger)->error("Remote player {} has no hand helper for hand {} presentation", GetNetworkID(), hand);
            }
            return;
        }

        const Shared::RPC::ItemClassId desired = handItemClass[hand];
        const bool wantsHolster                = !desired.IsValid() || desired != _shadowHandClass[hand];

        // Track shadow attachment locally; native inventory and hand state
        // can lag the attachment.
        const bool shadowIsOut = _shadowHandWuid[hand] != 0 && _appliedHandWuid[hand] == _shadowHandWuid[hand];

        // Select actuation by hand slot; torch item class alone does not
        // distinguish it.
        auto *shadowRuntime    = human->GetInventorySoulRuntime();
        auto *shadowInventory  = shadowRuntime != nullptr && shadowRuntime->IsInitialisedFor(human->GetSoul()) ? shadowRuntime->GetInventory() : nullptr;
        auto *shadowItem       = FindInventoryItemByWuid(shadowInventory, _shadowHandWuid[hand]);
        const bool shadowInSet = SDK::wh::entitymodule::C_ItemAttachmentManager::IsWeaponSetSlot(attachmentManager->FindSlotForItem(SDK::wh::framework::WUID {_shadowHandWuid[hand]}));

        if (wantsHolster) {
            // A drop replaces the held item with its ground replica; leave the
            // shadow alone until that replica has taken it over.
            if (!desired.IsValid() && _shadowHandClass[hand].IsValid() && KCDC::Features::GroundItem::GroundItemService::HasPendingHeldItemDrop(GetNetworkID(), _shadowHandClass[hand])) {
                setStage(HandItemStage::TransitionPending);
                return;
            }

            if (shadowIsOut) {
                // A weapon goes away through the engine's own holster action,
                // which is animated; the direct detach below is what makes it
                // vanish instead.
                if (shadowInSet && shadowItem != nullptr) {
                    if (IssueWeaponAction(human, hand, shadowItem, false)) {
                        setStage(HandItemStage::Holstered);
                    }
                    return;
                }

                // Torch slots use HolsterTorch; other items use the native
                // detach path.
                const bool holsterable = attachmentManager->GetSlotItemId(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch).value == _shadowHandWuid[hand];
                const bool detached    = holsterable ? attachmentManager->InvokeHolsterTorch() : attachmentManager->InvokeDetachItemById(SDK::wh::framework::WUID {_shadowHandWuid[hand]});

                // Cleared either way: an item left marked as held wedges
                // every later draw behind it.
                _appliedHandWuid[hand] = 0;
                if (!detached) {
                    if (setStage(HandItemStage::HolsterItemResolutionFailed)) {
                        Framework::Logging::GetLogger(kLogger)->error("Native holster refused remote player {} hand {} item {}", GetNetworkID(), hand, _shadowHandWuid[hand]);
                    }
                    return;
                }
                if (setStage(HandItemStage::Holstered)) {
                    Framework::Logging::GetLogger(kLogger)->info("Holstered remote player {} hand {} item {}", GetNetworkID(), hand, _shadowHandWuid[hand]);
                }
                return;
            }

            // An accepted holster is not a finished one: a rider's copy can
            // take the animated request and never play it, leaving the item
            // in the hand store. Once the action has had its time, the hand
            // is put away directly, as the engine's own un-animated holster
            // does.
            if (_shadowHandWuid[hand] != 0 && SDK::wh::entitymodule::IsItemDrawn(human, SDK::wh::framework::WUID {_shadowHandWuid[hand]}) && (_weaponActionAt == 0 || MafiaNet::GetTime() - _weaponActionAt >= kWeaponActionTimeoutMs)) {
                const bool torch = attachmentManager->GetSlotItemId(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch).value == _shadowHandWuid[hand];
                if (shadowInSet) {
                    attachmentManager->InvokeHolsterWeapons();
                }
                else if (torch) {
                    attachmentManager->InvokeHolsterTorch();
                }
                else {
                    attachmentManager->InvokeDetachItemById(SDK::wh::framework::WUID {_shadowHandWuid[hand]});
                }
                Framework::Logging::GetLogger(kLogger)->info("Forced holster of remote player {} hand {} item {} the native action left in hand", GetNetworkID(), hand, _shadowHandWuid[hand]);
            }

            if (!desired.IsValid()) {
                // The item stays in the copy's inventory; forgetting only
                // the class makes the next draw find it again rather than
                // create another.
                _shadowHandClass[hand] = {};
                setStage(HandItemStage::Holstered);
                return;
            }

            auto *runtime   = human->GetInventorySoulRuntime();
            auto *inventory = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetInventory() : nullptr;
            auto *equipment = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetEquipmentManager() : nullptr;
            if (inventory == nullptr || equipment == nullptr) {
                setStage(HandItemStage::WaitingInventoryRuntime);
                return;
            }

            const SDK::wh::entitymodule::S_ItemClassGUID heldClass {desired.low, desired.high};

            // Reuse weapons already received through replicated equipment.
            auto *held = inventory->InvokeFindItemByClassRaw(heldClass);
            if (held == nullptr) {
                // CreateItems reports failure from a path that runs after the
                // add, so this is attempt-bounded rather than retried forever.
                if (_handCreateAttempts[hand] >= kMaxCreateAttempts) {
                    if (setStage(HandItemStage::MaterializeFailed)) {
                        Framework::Logging::GetLogger(kLogger)->error("Gave up materializing remote player {} hand {} class {:016x}{:016x}", GetNetworkID(), hand, heldClass.m_low, heldClass.m_high);
                    }
                    return;
                }
                ++_handCreateAttempts[hand];
                inventory->CreateItems(heldClass, 1, false);
                held = inventory->InvokeFindItemByClassRaw(heldClass);
            }
            if (held == nullptr) {
                setStage(HandItemStage::MaterializeFailed);
                return;
            }
            _handCreateAttempts[hand] = 0;

            // Butchering holds a temporary MiscItem, not an equipped weapon.
            // The owner's native action attaches it directly to the hand.
            const bool tool = desired == Shared::Game::ItemClassIdFromGuid(Shared::Game::kButcheringToolClassGuid);
            if (!tool) {
            if (!equipment->IsInWeaponSlot(held)) {
                equipment->InvokeEquipItemRaw(held);
            }
            if (!equipment->IsInWeaponSlot(held)) {
                if (setStage(HandItemStage::EquipRefused)) {
                    Framework::Logging::GetLogger(kLogger)->error("Native equipment refused remote player {} hand {} class {:016x}{:016x}", GetNetworkID(), hand, heldClass.m_low, heldClass.m_high);
                }
                return;
            }
            }

            // Native player actions resolve their draw set through the owning
            // outfit's QAM. Raw equipment projection does not fill those slots.
            auto *playerInventory = runtime->GetPlayerInventorySoul();
            auto *outfits         = playerInventory != nullptr ? playerInventory->GetOutfitManager() : nullptr;
            auto *outfit          = outfits != nullptr ? outfits->GetOutfitState(outfits->GetCurrentOutfitId()) : nullptr;
            if (!tool && outfit != nullptr) {
                outfit->GetQuickAccessManager(0)->SynchronizeSlotsFromEquipment();

                // The sync can take a weapon off again: it unequips what it
                // does not keep in a QAM slot (0x18154C170), and once the copy
                // has holstered, that is the weapon about to be drawn. Off its
                // slot, the draw below would be the direct attach -- in the
                // hand, never drawn, so the copy plays no aim and no shot.
                // Equipped and then synced, it stays, as every draw that had
                // to equip it first shows.
                if (!equipment->IsInWeaponSlot(held)) {
                    equipment->InvokeEquipItemRaw(held);
                    outfit->GetQuickAccessManager(0)->SynchronizeSlotsFromEquipment();
                }
            }

            _shadowHandClass[hand] = desired;
            _shadowHandWuid[hand]  = held->GetId();
            setStage(HandItemStage::ShadowEquipped);
        }

        if (_shadowHandWuid[hand] == 0) {
            if (setStage(HandItemStage::LostShadow)) {
                Framework::Logging::GetLogger(kLogger)->error("Remote player {} has no native shadow to draw into hand {}", GetNetworkID(), hand);
            }
            return;
        }
        if (_appliedHandWuid[hand] == _shadowHandWuid[hand]) {
            setStage(HandItemStage::Drawn);
            return;
        }

        // Re-resolved rather than reused from above: on a first draw the
        // shadow was created a few lines ago, after the earlier lookup ran.
        auto *drawRuntime    = human->GetInventorySoulRuntime();
        auto *drawInventory  = drawRuntime != nullptr && drawRuntime->IsInitialisedFor(human->GetSoul()) ? drawRuntime->GetInventory() : nullptr;
        auto *drawItem       = FindInventoryItemByWuid(drawInventory, _shadowHandWuid[hand]);
        const bool drawInSet = SDK::wh::entitymodule::C_ItemAttachmentManager::IsWeaponSetSlot(attachmentManager->FindSlotForItem(SDK::wh::framework::WUID {_shadowHandWuid[hand]}));
        if (drawItem != nullptr && drawInSet) {
            if (IssueWeaponAction(human, hand, drawItem, true)) {
                setStage(HandItemStage::Drawn);
            }
            return;
        }

        // RequestItemHandling requires the movement-state item driver to be
        // installed.
        const auto attachHand           = static_cast<SDK::wh::entitymodule::C_ItemAttachmentManager::E_AttachHand::Type>(hand);
        const bool itemHandlingAccepted = attachmentManager->InvokeRequestItemHandling(kHeldItemHandlingMode);
        const bool attached             = itemHandlingAccepted && attachmentManager->InvokeAttachItemById(SDK::wh::framework::WUID {_shadowHandWuid[hand]}, attachHand);
        attachmentManager->InvokeFinishItemHandling();

        if (!itemHandlingAccepted) {
            if (setStage(HandItemStage::ItemHandlingRejected)) {
                Framework::Logging::GetLogger(kLogger)->error("Native item handling rejected remote player {} hand {} item {}", GetNetworkID(), hand, _shadowHandWuid[hand]);
            }
            return;
        }
        if (!attached) {
            if (setStage(HandItemStage::AttachFailed)) {
                Framework::Logging::GetLogger(kLogger)->error("Could not attach item {} to remote player {} hand {}", _shadowHandWuid[hand], GetNetworkID(), hand);
            }
            return;
        }

        // The attach finishes in a continuation, so this records the request
        // the engine accepted rather than a hand already holding it.
        _appliedHandWuid[hand] = _shadowHandWuid[hand];
        setStage(HandItemStage::Drawn);
    }

    bool Human::IssueWeaponAction(SDK::wh::entitymodule::C_Human *human, std::size_t hand, SDK::wh::entitymodule::C_Item *item, bool wantDrawn) {
        auto *actionActor = human->GetActionActor();
        if (actionActor == nullptr) {
            return false;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_weaponActionAt != 0 && now - _weaponActionAt < kWeaponActionTimeoutMs) {
            return false;
        }

        // Read the native hand store before choosing draw or holster.
        if (SDK::wh::entitymodule::IsItemDrawn(human, SDK::wh::framework::WUID {item->GetId()}) == wantDrawn) {
            _appliedHandWuid[hand] = wantDrawn ? item->GetId() : 0;
            return true;
        }

        // The other hand rides along when it is in the same set: one action
        // draws a sword and its shield together.
        const std::size_t otherHand = hand == Shared::Entities::HumanEntity::kRightHand ? Shared::Entities::HumanEntity::kLeftHand : Shared::Entities::HumanEntity::kRightHand;
        auto *manager               = human->GetItemAttachmentManager();
        if (manager == nullptr) {
            return false;
        }
        const auto slot = manager->FindSlotForItem(SDK::wh::framework::WUID {item->GetId()});
        if (!SDK::wh::entitymodule::C_ItemAttachmentManager::IsWeaponSetSlot(slot)) {
            return false;
        }
        auto *runtime               = human->GetInventorySoulRuntime();
        auto *inventory             = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetInventory() : nullptr;
        auto *otherItem             = FindInventoryItemByWuid(inventory, _shadowHandWuid[otherHand]);
        const auto otherSlot        = manager->FindSlotForItem(SDK::wh::framework::WUID {_shadowHandWuid[otherHand]});
        if (otherItem != nullptr && (!SDK::wh::entitymodule::E_WeaponEquipSlot::IsSameWeaponSet(slot, otherSlot) || (wantDrawn && (!handItemClass[otherHand].IsValid() || handItemClass[otherHand] != _shadowHandClass[otherHand])))) {
            otherItem = nullptr;
        }

        // A body playing an animation that holds it still (`lockMovement`),
        // as a script's lying loop does, plays no holster over that pose.
        // The set goes away at once, through the engine's own un-animated
        // holster: the one it gives a body knocked out with
        // `unconsciousHolsterInsteadDropWeapons` (0x180E7AA64, from the
        // unconscious action's Enter). Going down, no body is given the
        // animated one.
        if (!wantDrawn && animation.active && animation.lockMovement) {
            manager->InvokeHolsterWeapons();
            Framework::Logging::GetLogger(kLogger)->debug("Player {} put hand {} item {} away at once: an animation holds its body still", GetNetworkID(), hand, item->GetId());
            _appliedHandWuid[hand] = 0;
            if (otherItem != nullptr) {
                _appliedHandWuid[otherHand] = 0;
            }
            return true;
        }

        // The draw brings out whichever pair is current, so the set is
        // selected first. The engine folds slots to sets 0,1->0 2,3->1 4,5->2.
        // A different set cannot be selected while a hand is occupied. Let
        // the pending holster finish before queuing a draw for the new set.
        const auto set = static_cast<SDK::wh::entitymodule::E_WeaponSet::Type>(slot / 2);
        if (wantDrawn && manager->GetSelectedWeaponSet() != set && !manager->InvokeSetSelectedWeaponSet(set)) {
            return false;
        }

        SDK::wh::entitymodule::C_Item *pair[2] = {item, otherItem};
        const auto result                      = wantDrawn ? SDK::wh::entitymodule::DrawWeaponSetAnimated(actionActor, pair) : SDK::wh::entitymodule::HolsterWeaponSetAnimated(actionActor);
        if (!result.Accepted()) {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} weapon action refused for hand {} (status {})", GetNetworkID(), hand, result.status);
            return false;
        }

        _weaponActionAt = now;
        Framework::Logging::GetLogger(kLogger)->debug("Player {} accepted {} for hand {} item {} (action {})", GetNetworkID(), wantDrawn ? "draw" : "holster", hand, item->GetId(), result.actionId);

        // Both hands are marked against the one action, so the hand that did
        // not issue it does not ask for a second.
        _appliedHandWuid[hand] = wantDrawn ? _shadowHandWuid[hand] : 0;
        if (otherItem != nullptr) {
            _appliedHandWuid[otherHand] = wantDrawn ? _shadowHandWuid[otherHand] : 0;
        }
        return true;
    }

    void Human::ResetHandState(std::size_t hand) {
        _shadowHandClass[hand]    = {};
        _shadowHandWuid[hand]     = 0;
        _appliedHandWuid[hand]    = 0;
        _handCreateAttempts[hand] = 0;
        _remoteHandStage[hand]    = static_cast<std::uint8_t>(HandItemStage::None);
    }

    // --- Locomotion ----------------------------------------------------------

    void Human::ApplyNetworkMovement(const KCDC::Common::Replication::PoseSnapshot &pose, bool riding) {
        SDK::wh::entitymodule::C_Player *player     = GetPlayer();
        SDK::wh::entitymodule::C_PlayerInput *input = player != nullptr ? player->GetPlayerInput() : nullptr;
        if (input == nullptr || player->GetEntity() == nullptr) {
            return;
        }

        // Replication supplies the owner's input, as CNetPlayerInput does, and
        // it is replayed unaltered: the ordinary player PreUpdate owns
        // movement requests, sprint state, velocity, tags and the blend from
        // there. Drift is not steered out through it -- the position
        // correction rides this body's movement pass instead. The one
        // addition is in the saddle, where the X axis also carries the turn
        // the horse copy still owes its owner's facing.
        glm::vec2 axes    = glm::vec2(pose.input.AxisX(), pose.input.AxisY());
        const bool moving = pose.input.IsMoving();

        // The axes are relative to the owner's body: its ground movement
        // builds the world move from the entity rotation (0x1809D2920). This
        // body may face slightly differently while it turns to the same
        // facing, so the axes are carried into its frame. Not while a dodge
        // moves the body through the combat input listener, which uses the
        // dodge's own direction. In the saddle, the axes steer the horse
        // without conversion.
        if (moving && !riding && !KCDC::Features::Player::NativePlayerModule::WasMovedByCombatInput(player)) {
            const SDK::Quat bodyRotation = player->GetEntity()->GetWorldRotation();
            const float delta            = KCDC::Common::WrapPi(Shared::Entities::HumanEntity::YawOf(pose.rotation) - Shared::Entities::HumanEntity::YawOf(glm::quat(bodyRotation.w, bodyRotation.x, bodyRotation.y, bodyRotation.z)));
            const float c                = std::cos(delta);
            const float s                = std::sin(delta);
            axes                         = glm::vec2(axes.x * c - axes.y * s, axes.x * s + axes.y * c);
        }
        // In the saddle the axes go in through the stick's own handlers, so
        // the raw axes `BuildHorseMovementRequest` reads and the riding curve
        // the rider input is fed are this body's, made as the owner's were.
        // On foot they are left as held keys leave them: a stick here would
        // lift the walk floor the owner's keyboard kept.
        if (riding) {
            const SDK::wh::entitymodule::C_EntityActions *actions = SDK::wh::C_GlobalEnvironment::GetInstance()->GetEntityActions();
            // What the horse copy's facing is short of its owner's is ridden
            // out here (`Horse::PresentRemoteState`): this stick is the only
            // thing that turns a ridden horse.
            SDK::IActorSystem *actors                   = SDK::IActorSystem::GetInstance();
            const SDK::EntityId horseEntityId           = KCDC::Core::gApplication->GetHorseService().SeatOf(_entityId);
            const SDK::wh::entitymodule::C_Actor *mount = actors != nullptr && horseEntityId != 0 ? static_cast<const SDK::wh::entitymodule::C_Actor *>(actors->GetActor(horseEntityId)) : nullptr;
            axes.x                                      = std::clamp(axes.x + KCDC::Features::Player::NativePlayerModule::MountSteer(mount), -1.0F, 1.0F);
            input->OnXiMoveX(actions->GetXiMoveX(), axes.x);
            input->OnXiMoveY(actions->GetXiMoveY(), axes.y);
            ReplayRiderActions(player, pose);
            return;
        }
        input->SetMovementInput(SDK::Vec3 {axes.x, axes.y, 0.0F});
        input->SetRunMode(pose.input.run || pose.input.sprint);

        const bool sprint = pose.input.sprint && moving;
        player->GetLocalState()->SetSprintHeld(sprint);
        input->SetSprintRequested(sprint);
        // The owner has already accepted this state. Like CNetPlayerInput's
        // sprint event, enter the native transition without replaying the
        // keyboard hold delay on the receiving machine.
        if (sprint && !input->IsSprinting()) {
            input->StartSprint();
        }
        else if (!sprint && input->IsSprinting()) {
            input->StopSprint();
        }

        // The jump goes through the same bit the owner's jump key set, so the
        // movement HSM's ground pass starts it, gated and shaped as it was
        // there. Asked only while this body is not already in a jump: a jump
        // bit held in the air is a vault request. Pending until the jump
        // action starts, so one taken as the owner landed waits out this
        // body's own landing.
        const bool jumping = player->IsJumping();
        if (_jumpPendingUntil != 0) {
            if ((jumping && !_wasJumping) || MafiaNet::GetTime() >= _jumpPendingUntil) {
                _jumpPendingUntil  = 0;
                _jumpLaunchPending = jumping;
            }
            else if (!jumping && !IsBodyAirborne(player)) {
                input->RequestJump();
            }
        }

        // On the first airborne frame, use the owner's measured velocity
        // and the faster of the owner's rise and this body's own. The jump
        // is counted before takeoff, so the buffered pose can still blend
        // the owner's rise with a standing pose.
        // Later frames use the owner's horizontal velocity plus the buffered
        // position gap, closed over kAirSteerSeconds and capped at
        // kAirSteerMaxSpeed above that velocity. Physics controls the fall
        // and collisions; this path never sets the airborne position.
        if (_jumpLaunchPending && !jumping) {
            _jumpLaunchPending = false;
        }
        SDK::IPhysicalEntity *physics = player->GetEntity()->GetPhysics();
        SDK::pe_status_living status {};
        if (jumping && physics != nullptr && physics->GetStatus(&status) != 0 && status.bFlying != 0) {
            const SDK::Vec3 at  = player->GetEntity()->GetWorldPos();
            glm::vec2 catchUp   = (glm::vec2(pose.position) - glm::vec2(at.x, at.y)) / kAirSteerSeconds;
            const float speedUp = glm::length(catchUp);
            if (speedUp > kAirSteerMaxSpeed) {
                catchUp *= kAirSteerMaxSpeed / speedUp;
            }
            SDK::pe_action_set_velocity steer;
            steer.v = SDK::Vec3 {pose.velocity.x + catchUp.x, pose.velocity.y + catchUp.y, _jumpLaunchPending ? std::max(pose.velocity.z, status.vel.z) : status.vel.z};
            physics->Action(&steer);
            _jumpLaunchPending = false;
        }
        _wasJumping = jumping;
    }

    void Human::ReplayRiderActions(SDK::wh::entitymodule::C_Player *player, const KCDC::Common::Replication::PoseSnapshot &pose) {
        // The gait keys reach this body's rider input as the owner's reached
        // theirs: through the handlers its player input offers every action
        // to first. Each press is handed over, so a second one inside the
        // handler's double-press window spurs here as it did there; the held
        // gallop is held, and the walk toggle is pressed only when this rider
        // stands on the other side of it.
        SDK::wh::entitymodule::C_RiderPlayerInput *rider      = player->GetRiderPlayerControl()->GetRiderInput();
        const SDK::wh::entitymodule::C_EntityActions *actions = SDK::wh::C_GlobalEnvironment::GetInstance()->GetEntityActions();
        // A pose from before the owner's rider input had its horse speaks of
        // the body on foot: nothing in it is the rider's, and the counts it
        // carries are not a base to replay from.
        if (!pose.input.inSaddle) {
            _riderPressesKnown = false;
            return;
        }
        if (!_riderPressesKnown) {
            _riderPressesKnown      = true;
            _appliedSprintPresses   = pose.input.sprintPresses;
            _appliedSlowdownPresses = pose.input.slowdownPresses;
        }
        while (_appliedSprintPresses != pose.input.sprintPresses) {
            _appliedSprintPresses = static_cast<std::uint8_t>((_appliedSprintPresses + 1) & Shared::Entities::PlayerInput::kCountMask);
            rider->HandleAction(actions->GetSprint(), SDK::wh::entitymodule::C_RiderPlayerInput::ActivationPress, 1.0F);
        }
        // One brake per press, as `horse_slowdown` is press-only.
        while (_appliedSlowdownPresses != pose.input.slowdownPresses) {
            _appliedSlowdownPresses = static_cast<std::uint8_t>((_appliedSlowdownPresses + 1) & Shared::Entities::PlayerInput::kCountMask);
            rider->OnSlowdown();
        }
        if (pose.input.sprint) {
            rider->HandleAction(actions->GetSprint(), SDK::wh::entitymodule::C_RiderPlayerInput::ActivationHold, 1.0F);
        }
        else if (rider->IsSprintHeld()) {
            rider->HandleAction(actions->GetSprint(), SDK::wh::entitymodule::C_RiderPlayerInput::ActivationRelease, 0.0F);
        }
        if (rider->IsWalkToggled() == pose.input.run) {
            rider->HandleAction(actions->GetToggleRun(), SDK::wh::entitymodule::C_RiderPlayerInput::ActivationRelease, 0.0F);
        }
    }

    bool Human::IsBodyAirborne(SDK::wh::entitymodule::C_Player *player) {
        // The jump bit held in the air is a vault request to PreUpdate, so it
        // is only ever set on a body standing on something.
        SDK::IPhysicalEntity *physics = player->GetEntity() != nullptr ? player->GetEntity()->GetPhysics() : nullptr;
        SDK::pe_status_living status {};
        return physics != nullptr && physics->GetStatus(&status) != 0 && status.bFlying != 0;
    }

    void Human::WithdrawLocomotion() {
        // A jump that arrived while something else owned the body is spent:
        // replayed once that ends, it would jump the body a moment later
        // than its owner and for no reason the body still has.
        _jumpPendingUntil = 0;
        if (SDK::wh::entitymodule::C_Player *player = GetPlayer()) {
            player->GetLocalState()->SetSprintHeld(false);
            if (SDK::wh::entitymodule::C_PlayerInput *input = player->GetPlayerInput()) {
                input->SetMovementInput(SDK::Vec3 {});
                input->SetSprintRequested(false);
                if (input->IsSprinting()) {
                    input->StopSprint();
                }
            }
        }
    }

    void Human::SampleLookPitch(KCDC::Common::Replication::PoseSnapshot &pose) const {
        pose.lookPitch = headLookReady && KCDC::Common::IsFinite(headLookDirection) ? std::asin(std::clamp(headLookDirection.z, -1.0F, 1.0F)) : 0.0F;
    }

    // --- Riding --------------------------------------------------------------

    void Human::ApplyRide(SDK::EntityId horseEntityId, bool playGetOn) {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(KCDC::Features::Player::NativePlayerModule::IsRemote(_nativePlayer));
        // A corpse carries nobody, whatever the wire still says: the server
        // takes the rider off in the same breath as it declares the death,
        // but the two need not land in the same frame.
        auto &horses = KCDC::Core::gApplication->GetHorseService();
        if (horseEntityId != 0 && horses.IsDeadHorse(horseEntityId)) {
            horseEntityId = 0;
        }

        // Recorded before anything can turn the call away, because the
        // presentation reads it whether or not a body is up to seat.
        _wireHorseEntityId = horseEntityId;

        if (_entityId == 0) {
            return;
        }

        auto *actorSystem                      = SDK::IActorSystem::GetInstance();
        SDK::wh::entitymodule::C_Player *actor = GetPlayer();
        if (actor == nullptr) {
            return;
        }

        // Use the horse's seat state; queued ride actions start earlier and
        // end later.
        const SDK::EntityId riding = horses.SeatOf(_entityId);
        if (riding == 0 && horseEntityId == 0) {
            _seenOnFoot = true;
        }

        // Settled in the saddle the wire names (or on foot with none): the
        // replayed stick in this body's input is what steers the horse copy
        // through its rider input, so it is left alone.
        if (riding == horseEntityId) {
            return;
        }

        // QueueAction can enter the ride inline. Give up walking BEFORE it
        // does: otherwise its first requests are vetoed and next frame's
        // steering cleanup clears targets that already belong to the mount.
        WithdrawLocomotion();

        // Finish dismounting before requesting another saddle. Asking again
        // while the get-off is already running drives the state machine back
        // through the transition it is playing, and `StopRiding` cannot warn
        // of it: it answers true for a request that moved the ride, for one
        // that found both flanks blocked and did nothing, and for a body with
        // no ride at all. The state is what says a get-off started.
        if (riding != 0) {
            // A forced get-off already asked for is finishing: the rider
            // action's Exit releases the seat on its next update.
            if (_forcedOffSaddle) {
                return;
            }

            // A death in the saddle kicks the rider out: on the rider's own
            // machine the ride ended with the game's forced get-off the moment
            // either body died (HorseService::HoldDeathInSaddle), so this body
            // is kicked out the same way rather than playing a get-off its
            // owner never played. A corpse could not play one anyway. The
            // copy of a dead horse stays standing until the seat is empty
            // (Horse::PresentLife), a frame or two.
            auto *soul = actor->GetSoul();
            if ((soul != nullptr && soul->IsDead()) || horses.IsDeadHorse(riding)) {
                ForceOffSaddle(actor);
                return;
            }

            // The game's own get-off, the one a living rider plays and the
            // one a voluntary dismount replicates as. A get-off already
            // playing is left alone, a mount still playing refuses one until
            // it lands, and a ride at none is one whose Exit has yet to run.
            // So is a rear or a throw the horse is playing (`Horse` replays
            // the owner's): the owner's ride ended when its throw did, which
            // empties the seat while this one is still being thrown, and a
            // get-off asked for now would cut it short. And so is a ride its
            // owner fell from, before this body is drawn where the owner
            // fell: the fall ends it there (PresentRemoteState).
            const auto state = actor->GetRideState();
            if (state == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_DISMOUNTING || state == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_MOUNTING || state == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE
                || state == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_7 || state == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_8 || fallSequence != _appliedFallSequence) {
                return;
            }

            // Through the rider expansion, not StopRiding: a player body's
            // slot 205 only raises a dismount request for its own rider
            // input, which does not steer a replicated horse. On the flank
            // the owner's game chose: chosen here, it could be the other one.
            actor->BeginGetOff(horses.DismountSideOf(riding));
            if (actor->GetRideState() == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_DISMOUNTING) {
                Framework::Logging::GetLogger(kLogger)->info("'{}' is getting off horse entity {}", nickname, riding);
                return;
            }

            // Both flanks blocked: no get-off can be measured, so the ride
            // ends the way the rider's own game ended it.
            ForceOffSaddle(actor);
            Framework::Logging::GetLogger(kLogger)->info("'{}' was set down from horse entity {}: no room for a get-off", nickname, riding);
            return;
        }

        if (horseEntityId == 0) {
            return;
        }

        // One mount in flight at a time. The queue enters it inline when the
        // slot is free and later when it is not -- never while the body
        // sleeps -- and asking again every frame meanwhile stacks rider
        // actions behind it. A refused one is asked for again once the horse
        // or this body wakes. Noted before queueing, because an inline entry
        // answers from inside the call.
        if (horses.IsMountOutstanding(_entityId, horseEntityId)) {
            return;
        }

        // Streaming recreates the bodies before their animation controllers.
        // Leave the mount unqueued so the next frame can try once both exist.
        // Enter_Hook still checks, for an entry the queue holds back.
        auto *horse = static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(horseEntityId));
        if (!KCDC::Features::Horse::HorseService::IsBodyReadyToRide(actor) || !KCDC::Features::Horse::HorseService::IsBodyReadyToRide(horse)) {
            return;
        }

        // C_Player's MountHorse (slot 204, 0x1819A6250) is the rider
        // expansion's StartRide with the player flag, behind a refusal while
        // the soul's carried weight is past the heavy-load threshold. The
        // owner passed that check when it mounted; this copy's soul carries
        // only the replicated share of its load plus hand-item shadows, so
        // asking again could only disagree -- StartRide is asked directly.
        //
        // Its `force` is the one byte between the two ways into the saddle
        // (C_ActorActionRider's parameter word, byte 4): clear, the rider
        // action plays the get-on the owner's interaction played, pairing its
        // slave action on the horse copy -- which only its rider's replayed
        // input moves, and that stands still through a get-on -- set, it seats
        // the body as Lua's ForceMount does. A get-on watched here plays; an
        // instant seat or a ride joined under way is seated, and so is a
        // get-on the game refuses to play, such as one with no room at the
        // horse's flank.
        SDK::wh::entitymodule::C_RiderExpansion *rider = actor->GetRiderExpansion();
        if (rider == nullptr) {
            return;
        }
        const bool animated = playGetOn && _seenOnFoot;
        horses.NoteMountQueued(_entityId, horseEntityId);
        if ((!animated || !rider->StartRide(horse, false, true)) && !rider->StartRide(horse, true, true)) {
            horses.ForgetMountInFlight(_entityId);
            return;
        }
        Framework::Logging::GetLogger(kLogger)->info("'{}' is {} horse entity {}", nickname, animated ? "mounting" : "seated on", horseEntityId);
    }

    void Human::AbandonRide() {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(KCDC::Features::Player::NativePlayerModule::IsRemote(_nativePlayer));
        if (!IsSeated()) {
            return;
        }

        if (SDK::wh::entitymodule::C_Player *actor = GetPlayer()) {
            ForceOffSaddle(actor);
            Framework::Logging::GetLogger(kLogger)->info("'{}' was taken off a horse that is going away", nickname);
        }
    }

    void Human::ForceOffSaddle(SDK::wh::entitymodule::C_Actor *actor) {
        _forcedOffSaddle = true;
        KCDC::Features::Horse::HorseService::EndRide(actor);
    }

    bool Human::IsSeated() const {
        // In a saddle or in a cart: either way the engine has the body attached
        // to what carries it, and a pose stated here would pull it off.
        return KCDC::Core::gApplication->GetHorseService().SeatOf(_entityId) != 0 || KCDC::Features::Cart::CartService::Get().IsInCart(_entityId);
    }

    // --- Carrying ------------------------------------------------------------

    void Human::ApplyCarry(SDK::wh::entitymodule::C_Actor *actor) {
        // The body the server says is on this player's shoulder, as this
        // machine's copy of it: the local player's own body when it is ours.
        SDK::wh::entitymodule::C_Actor *target = nullptr;
        if (Human *carried = KCDC::Common::FindReplicaById<Human>(carriedNetworkId)) {
            target = carried->ResolveBody();
        }

        // Nobody carries the living -- the rule the game's own grab prompt
        // applies, and the one a revival leans on: the body gets up on the
        // spot, while the server's word that the carry is over is still on
        // its way. So the wire is only believed about a body that is down.
        const SDK::wh::rpgmodule::I_Soul *soul = target != nullptr ? target->GetSoul() : nullptr;
        const SDK::EntityId wanted             = soul != nullptr && (soul->IsDead() || soul->IsUnconscious()) ? target->GetEntityId() : 0;

        // A carry already under way when this body is first seen was picked
        // up before this machine was watching, and is joined on the shoulder;
        // one that ended before it was ever joined leaves nothing to join.
        if (!_carrySeen) {
            _carrySeen       = true;
            _carryStreamedIn = carriedNetworkId != 0;
        }
        if (carriedNetworkId == 0) {
            _carryStreamedIn = false;
        }

        // What the engine holds, not what was last asked of it: a put-down,
        // a death or a released body all end a carry without asking here.
        SDK::wh::entitymodule::I_ActorCarryCorpse *held = actor->FindCarryCorpse();
        const SDK::EntityId holding                     = held != nullptr && held->HasTarget() ? held->GetTargetEntityId() : 0;
        if (holding == wanted) {
            if (wanted != 0) {
                _carryStreamedIn   = false;
                _carryGrabAskedFor = 0;
            }
            return;
        }

        // Let go of what it holds before taking anything else. The put-down
        // the owner played is played here too; one caught before the body
        // reached the shoulder, which has no put-down, is let go outright.
        if (holding != 0) {
            if (held->IsPuttingDown()) {
                return;
            }
            if (held->IsCarrying()) {
                held->RequestPutDown();
                Framework::Logging::GetLogger(kLogger)->info("'{}' is putting a body down", nickname);
            }
            else {
                KCDC::Core::gApplication->GetPlayerService().ReleaseCarryBy(_entityId);
            }
            return;
        }

        if (wanted == 0 || !actor->IsSettled() || !target->IsSettled()) {
            return;
        }

        // Every grab the game makes -- the prompt, a behaviour tree's
        // PickUpBody -- is asked of `CanGrabCorpse` (0x1812EB090) first, and
        // that gate reads the body's actor model, not its soul: dead or
        // unconscious there, or an unconscious soul, and not already on a
        // shoulder. A body whose soul is dead can still be standing in its
        // model -- its death not yet presented, a revival already under way
        // -- and such a body refuses the Carried action the pick-up binds it
        // with, which `BindTargetToCarrier` (0x18285CB88) then reads anyway.
        // Only the gate's target half is asked here: its carrier half wants
        // an idle body, and that was the owner's to be, not this copy's.
        const SDK::wh::entitymodule::C_ActorModel *targetModel = target->GetActorModel();
        const bool down                                        = targetModel->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Dead) || targetModel->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Unconscious) || soul->IsUnconscious();
        if (!down || targetModel->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Carried)) {
            return;
        }

        // A grab is an action that enters on the queue's next update; asking
        // again before then would queue a second one behind it.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_carryGrabAskedFor == wanted && now - _carryGrabAskedAt < kCarryGrabTimeoutMs) {
            return;
        }

        SDK::wh::entitymodule::I_ActorCarryCorpse *carry = actor->GetCarryCorpse();
        const std::uint32_t queued                       = _carryStreamedIn ? carry->RequestGrabInstant(wanted) : carry->RequestGrab(wanted);
        _carryGrabAskedFor                               = wanted;
        _carryGrabAskedAt                                = now;
        if (queued != 0) {
            Framework::Logging::GetLogger(kLogger)->info("'{}' is picking up entity {}{}", nickname, wanted, _carryStreamedIn ? " (already on the shoulder)" : "");
        }
    }

    void Human::ApplyCarriedItem(SDK::wh::entitymodule::C_Actor *actor) {
        auto &players                                          = KCDC::Core::gApplication->GetPlayerService();
        SDK::wh::entitymodule::C_ActorActionCarryItem *running = players.ItemCarryActionFor(_entityId);
        if (HasStationPose()) {
            if (running != nullptr) {
                running->Finish();
            }
            return;
        }

        // What the engine runs, not what was last asked of it: a put-down, a
        // pile or an interruption all end a carry without asking here.
        if (running != nullptr) {
            const SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::Type phase = running->GetPhase();
            const bool kept                                                          = carriedItemClass.IsValid() && players.ItemCarryClassFor(_entityId) == carriedItemClass;
            if (kept) {
                _itemCarryAskedAt  = 0;
                _itemCarryAttempts = 0;
            }
            // The owner's put-down started: it is played here as it plays
            // there, once this copy has the item in its arms. Until then the
            // carry stands, and the end of the owner's put-down ends it.
            if (kept && (!carriedItemPuttingDown || phase != SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::Carrying)) {
                return;
            }

            // Let go of what it carries before taking anything else. The
            // put-down the owner played is played here too; a carry still
            // being picked up has none, and is ended outright.
            if (phase == SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::PuttingDown || phase == SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::Dropped || _itemPutDownAsked) {
                return;
            }
            _itemPutDownAsked = true;
            if (phase == SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::Carrying) {
                Framework::Logging::GetLogger(kLogger)->info("'{}' is putting an item down", nickname);
                running->BeginPutDownAnimation();
            }
            else {
                running->Finish();
            }
            return;
        }
        _itemPutDownAsked = false;

        // As the owner's was: straight into the hands, or up off the ground.
        if (carriedItemClass != _itemCarryAttemptClass) {
            _itemCarryAttemptClass = carriedItemClass;
            _itemCarryAttempts     = 0;
        }
        if (!carriedItemClass.IsValid() || !actor->IsSettled() || !_appliedSoul.CanAct()) {
            return;
        }
        // Every ask makes an item, and a body that refuses one refuses the
        // next: a few asks, then this carry is left unreplayed.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_itemCarryAttempts >= kMaxItemCarryAttempts || (_itemCarryAskedAt != 0 && now - _itemCarryAskedAt < kItemCarryRetryMs)) {
            return;
        }
        _itemCarryAskedAt = now;
        if (++_itemCarryAttempts == kMaxItemCarryAttempts) {
            Framework::Logging::GetLogger(kLogger)->warn("'{}' carries an item this body keeps refusing; giving up after {} tries", nickname, kMaxItemCarryAttempts);
        }

        // Taken up from a prop: this machine's own copy of it, with the
        // game's pick-up, as the owner did. A prop already gone from here --
        // a carry seen long after it began -- is stood in for by a made item.
        auto *prop = carriedGroundItemNetworkId != 0 ? KCDC::Common::FindReplicaById<KCDC::Features::GroundItem::GroundItem>(carriedGroundItemNetworkId) : nullptr;
        if (prop != nullptr && players.StartGroundItemCarry(actor, prop)) {
            return;
        }
        players.StartItemCarry(actor, carriedItemClass, 0);
    }

    // --- Speech --------------------------------------------------------------

    // --- Appearance ----------------------------------------------------------

    bool Human::SoulWearsGender(const SDK::wh::rpgmodule::I_Soul *soul, Shared::Game::AppearanceGender gender) {
        if (soul == nullptr) {
            return false;
        }
        const SDK::wh::entitymodule::E_ActorGender wanted = gender == Shared::Game::AppearanceGender::Female ? SDK::wh::entitymodule::E_ActorGender::Female : SDK::wh::entitymodule::E_ActorGender::Male;
        return soul->GetGender() == wanted;
    }

    std::string Human::EffectiveHead(const SDK::wh::rpgmodule::I_Soul *soul) const {
        const std::string &wanted = appearance.Part(Shared::Game::AppearancePart::Head);
        if (!wanted.empty() || soul == nullptr) {
            return wanted;
        }

        // Borrowed, so copied out before anything writes to the soul again.
        return soul->BodyDescriptionPart(SDK::wh::rpgmodule::S_CharacterBodyDescription::kHead);
    }

    std::array<Human::BodyProperty, 3> Human::BodyProperties(Shared::Game::AppearanceGender gender) {
        // Spelled as NPC.lua / player.lua and NPC_Female.lua spell them,
        // because C_Actor::LoadModel compares models with a plain stricmp.
        const char *skeleton = gender == Shared::Game::AppearanceGender::Female ? "Objects/Characters/humans/female/skeleton/female.cdf" : "Objects/Characters/humans/male/skeleton/male.cdf";
        return {{
            {"fileModel", skeleton},
            {"clientFileModel", skeleton},
            {"esClothingConfig", gender == Shared::Game::AppearanceGender::Female ? "female2" : "male2"},
        }};
    }

    void Human::ApplyGenderTag(SDK::wh::entitymodule::C_Actor *actor, Shared::Game::AppearanceGender gender) {
        SDK::CTagState *tags               = actor != nullptr ? actor->GetTagState() : nullptr;
        const SDK::CTagDefinition *defined = tags != nullptr ? tags->GetDefinition() : nullptr;
        if (defined == nullptr) {
            return;
        }
        tags->SetTag(defined->FindCRC(kFemaleTagCrc), gender == Shared::Game::AppearanceGender::Female);
    }

    void Human::MarkWomansController(SDK::IActionController *controller) {
        SDK::SAnimationContext *context    = controller != nullptr ? controller->GetContext() : nullptr;
        SDK::CTagState *tags               = context != nullptr ? &context->GetTagState() : nullptr;
        const SDK::CTagDefinition *defined = tags != nullptr ? tags->GetDefinition() : nullptr;
        if (defined == nullptr) {
            return;
        }
        tags->SetTag(defined->FindCRC(kFemaleTagCrc), true);
    }

    int Human::PairedGenderArchetype(int archetypeId, Shared::Game::AppearanceGender wanted) {
        // The two human pairs soul_archetype.xml ships: rows that differ
        // only in gender. Nothing derives one from the other.
        struct Pair {
            int male;
            int female;
        };
        static constexpr Pair kHumanPairs[] = {
            {0, 1},   // NPC / NPC_Female
            {13, 15}, // Hero / Hero_female
        };

        for (const Pair &pair : kHumanPairs) {
            if (archetypeId != pair.male && archetypeId != pair.female) {
                continue;
            }
            return wanted == Shared::Game::AppearanceGender::Female ? pair.female : pair.male;
        }
        return -1;
    }

    bool Human::ApplyAppearance(SDK::wh::rpgmodule::I_Soul *soul) {
        if (soul == nullptr) {
            return false;
        }

        // Held against the appearance that earned it, so either changing
        // retries.
        const auto refuse = [this]() {
            _appearanceDirty   = false;
            _appearanceRefused = true;
            return false;
        };

        // Checked on every receiver rather than trusted from the sender.
        if (!appearance.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing an unknown appearance for '{}' (networkId {})", nickname, GetNetworkID());
            return refuse();
        }

        // Taken before anything moves the archetype: the gender the soul's own
        // body, head and hair were authored for.
        if (!_soulGenderKnown) {
            _soulGender      = soul->GetGender() == SDK::wh::entitymodule::E_ActorGender::Female ? Shared::Game::AppearanceGender::Female : Shared::Game::AppearanceGender::Male;
            _soulGenderKnown = true;
        }

        // The parts mean nothing until the soul's gender agrees with them. An
        // appearance with no parts still carries a gender: it asks for the
        // default body of that gender, and the skeleton a copy is spawned on
        // follows it too, so the owner's soul has to as well.
        if (!SoulWearsGender(soul, appearance.gender)) {
            // The archetype carries four authored numbers with the gender:
            // body_base_armor, the conspicuousness/visibility pair and
            // unarmed_attack_base. Nothing reads them on a copy; on the
            // body this client plays they are a real change, which is why
            // the pair table is narrow and nothing reaches here unless a
            // server asked for the other gender.
            //
            // The re-resolve the setter runs is not itself exotic --
            // C_RPGModule::ResolveSoulForActor runs the same function on
            // live souls -- and its dog and horse branches are keyed on
            // race, which both human archetypes share.
            const int paired = PairedGenderArchetype(soul->GetArchetypeId(), appearance.gender);
            if (paired < 0 || !soul->SetArchetypeId(paired) || !SoulWearsGender(soul, appearance.gender)) {
                Framework::Logging::GetLogger(kLogger)->warn("Body of '{}' (networkId {}) cannot wear a {} appearance; leaving it as it is", nickname, GetNetworkID(), appearance.gender == Shared::Game::AppearanceGender::Female ? "female" : "male");
                return refuse();
            }
        }

        // A beard's .skin loads from the head's own folder, so one nobody
        // modelled for this face resolves to a file that is not there: no
        // attachment, no error, a body that comes back clean-shaven. Checked
        // against the head this write leaves behind, not the one asked for.
        const std::string &beard = appearance.Part(Shared::Game::AppearancePart::Beard);
        if (!beard.empty()) {
            const std::string head = EffectiveHead(soul);
            if (!Shared::Game::IsBeardOnHead(appearance.gender, head, beard)) {
                Framework::Logging::GetLogger(kLogger)->warn("'{}' (networkId {}) cannot wear beard '{}' on head '{}'; the game ships no such beard for that face", nickname, GetNetworkID(), beard,
                    head.empty() ? Shared::Game::DefaultAppearanceHead(appearance.gender) : std::string_view(head));
                return refuse();
            }
        }

        // Each write rebuilds the character, so only what moved is touched
        // -- and a body nobody chose for reaches none of them, an all-empty
        // appearance matching the all-empty one a fresh soul already wears.
        //
        // Except on a soul worn as the other gender: its own parts are names
        // from the other tree, which this gender's clothing resolves to
        // nothing -- a body with no skin, no face and no hair, only its
        // clothes and nametag. Henry's soul worn as a woman is the case. A
        // part the appearance leaves empty is then that gender's default.
        const bool foreignSoul = appearance.gender != _soulGender;
        for (std::size_t part = 0; part < Shared::Game::CharacterAppearance::kPartCount; ++part) {
            std::string wanted = appearance.parts[part];
            if (wanted.empty() && foreignSoul) {
                wanted = std::string(DefaultPartForGender(static_cast<Shared::Game::AppearancePart>(part), appearance.gender));
            }
            if (wanted == _lookAsParts[part]) {
                continue;
            }
            _lookAsParts[part] = wanted;

            if (wanted.empty()) {
                // Cleared rather than written empty, so the soul's own
                // layers answer the part again.
                soul->ClearOverrideBodyDescriptionPart(static_cast<std::uint8_t>(part));
                continue;
            }

            soul->SetBodyDescriptionPart(static_cast<std::uint8_t>(part), SDK::ScopedCryString(wanted.c_str()), SDK::wh::rpgmodule::S_CharacterBodyDescription::kLayerLookAs);
        }

        _appliedAppearance = appearance;
        _appearanceDirty   = false;
        _appearanceRefused = false;
        return true;
    }

    bool Human::AdoptAppearance(const Shared::Game::CharacterAppearance &wanted) {
        if (!IsOwner()) {
            return false;
        }
        if (appearance == wanted) {
            return true;
        }

        const Shared::Game::CharacterAppearance previous = appearance;
        appearance                                       = wanted;
        _appearanceDirty                                 = true;
        _appearanceRefused                               = false;

        auto *actor = KCDC::Common::LocalHumanActor();
        if (actor != nullptr && !ApplyAppearance(actor->GetSoul())) {
            appearance = previous;
            return false;
        }
        if (actor != nullptr) {
            KCDC::Core::gApplication->GetPlayerService().RequestLocalBody(appearance.gender);
        }
        return true;
    }

    void Human::Talk(const std::string &text) {
        const KCDC::Features::Player::NativePlayerModule::RemoteScope remoteScope(KCDC::Features::Player::NativePlayerModule::IsRemote(_nativePlayer));
        if (text.empty()) {
            return;
        }

        SDK::wh::entitymodule::C_Player *actor = GetPlayer();
        if (actor == nullptr) {
            // No body yet, and a spoken line is an event: nothing to replay.
            return;
        }

        auto *speakMan = actor->GetSpeakMan();
        if (speakMan == nullptr) {
            return;
        }

        // Speak copies these strings through the engine allocator. Keep the
        // empty sound key alive through the call.
        const SDK::ScopedCryString sound("");
        const SDK::ScopedCryString line(text.c_str());
        const SDK::ScopedCryString mood("");

        SDK::wh::entitymodule::S_SpeakData data {};
        data.soundEvent = sound.Get();
        data.text       = line.Get();
        data.mood       = mood.Get();
        data.unk0019    = 1;

        // Raw typing, so no lookup. Every player is somebody's local player, so
        // the second has no answer here; it only gates a facial pass.
        data.isLocalizable      = false;
        data.isPlayerControlled = false;

        // Speak's first gate asks whether the body is in a dialogue.
        data.force = true;

        // Long enough to read, floored so a short line does not blink out.
        data.duration = std::max(3.0F, static_cast<float>(text.size()) * 0.06F);

        // Negative keeps the game's own bubble distance.
        data.maxDistance = -1.0F;

        // Without this, a line replacing a live bubble keeps the old text.
        speakMan->HideBubble();
        speakMan->Speak(data);
    }

    // --- Disguise ------------------------------------------------------------

    namespace {
        /** Where every human body keeps its character; see RefreshAnimationBinding. */
        constexpr int kCharacterSlot = 0;

        /** `IEntity::GetStatObj` reads bit 31 as "an entity slot", stock `ENTITY_SLOT_ACTUAL`, rather than a compound part. */
        constexpr int kActualSlot = static_cast<int>(0x80000000U);

        // Bounds on the cylinder a mesh may ask for. The floor keeps a sliver
        // of a mesh -- a plank edge-on -- from making a body nothing can hit;
        // the ceiling keeps a wagon from making one that cannot pass a door.
        constexpr float kMinDisguiseRadius     = 0.15F;
        constexpr float kMaxDisguiseRadius     = 1.5F;
        constexpr float kMinDisguiseHeight     = 0.3F;
        constexpr float kMaxDisguiseClearance  = 0.5F;
        constexpr float kColliderHoldTolerance = 0.01F;

        bool SameCylinder(const SDK::pe_player_dimensions &a, const SDK::pe_player_dimensions &b) {
            return std::fabs(a.sizeCollider.x - b.sizeCollider.x) <= kColliderHoldTolerance && std::fabs(a.sizeCollider.z - b.sizeCollider.z) <= kColliderHoldTolerance && std::fabs(a.heightCollider - b.heightCollider) <= kColliderHoldTolerance;
        }
    } // namespace

    void Human::AdoptDisguise(const Shared::Game::PlayerDisguise &wanted) {
        if (!IsOwner() || disguise == wanted) {
            return;
        }
        // Published at once: CaptureLocalState puts it on the local body on
        // this same frame, and nothing can refuse it there.
        disguise = wanted;
        Framework::Logging::GetLogger(kLogger)->info("Disguised as '{}'", disguise.IsEmpty() ? std::string("themselves") : disguise.model);
    }

    void Human::ApplyDisguise(SDK::IEntity *entity) {
        if (entity->GetId() != _disguiseEntityId) {
            ForgetDisguise();
            _disguiseEntityId = entity->GetId();
        }

        if (!(disguise == _appliedDisguise)) {
            // KCD2 rebuilds a body's character asynchronously -- after a spawn
            // and on every re-dress -- and for a moment the entity has no
            // slots at all. `LoadGeometry(-1)` appends at the end, which is
            // then slot 0: the mesh lands where the character is about to,
            // and freeing it later frees the character, leaving Mannequin's
            // aim context on a dead pose blender. So a disguise waits for the
            // character, and goes in the slot after it.
            if (!disguise.IsEmpty() && entity->GetCharacter(kCharacterSlot) == nullptr) {
                return;
            }
            // A streamed mesh published after this client mounted its paks waits for the
            // generation that carries it, as a body waits for its character.
            if (disguise.IsStreamed() && !KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(disguise.model)) {
                return;
            }
            WithdrawDisguise(entity);
            _appliedDisguise = disguise;
            // A receiver checks what it is sent; a mesh it cannot vouch for
            // is left off rather than half applied.
            if (!disguise.IsEmpty() && disguise.IsSane()) {
                _disguiseSlot = entity->LoadGeometry(std::max(entity->GetSlotCount(), kCharacterSlot + 1), disguise.model.c_str());
                if (_disguiseSlot < 0) {
                    Framework::Logging::GetLogger(kLogger)->error("Could not load '{}' to disguise '{}' (entity {}); the body stays as it is", disguise.model, nickname, entity->GetId());
                }
            }
        }

        if (_disguiseSlot < 0) {
            return;
        }

        _drawHeldByDisguise = true;
        SuppressCharacterDraw(entity);

        auto *physics = entity->GetPhysics();
        if (physics == nullptr) {
            return;
        }
        if (!_disguiseColliderFitted) {
            _disguiseColliderFitted = FitDisguiseCollider(entity);
            return;
        }
        if (_disguiseColliderRefused) {
            return;
        }

        // Held rather than set once: a stance change states the body's own
        // cylinder again.
        SDK::pe_player_dimensions live;
        if (physics->GetParams(&live) != 0 && !SameCylinder(live, _disguiseDimensions)) {
            physics->SetParams(&_disguiseDimensions);
        }
    }

    void Human::SuppressCharacterDraw(SDK::IEntity *entity) {
        SDK::ICharacterInstance *character = entity->GetCharacter(kCharacterSlot);
        if (character == nullptr) {
            return;
        }

        // What this character had before anything touched it, taken once per
        // character: re-dressing can build a new one in the slot, which
        // starts drawing again and is hidden in turn.
        if (character != _suppressedCharacter) {
            _suppressedCharacter              = character;
            _suppressedCharacterDrew          = (character->GetFlags() & SDK::CS_FLAG_DRAW_MODEL) != 0;
            _suppressedCharacterUpdatedAlways = (character->GetFlags() & SDK::CS_FLAG_UPDATE_ALWAYS) != 0;
        }

        // The character's own draw gate, which its attachments read too: the
        // entity slot's render flag does not stop KCD2 drawing a body. Still
        // animated, because the body needs its skeleton -- the camera, the
        // hands and the bone colliders all read it.
        const int flags = character->GetFlags();
        if ((flags & SDK::CS_FLAG_DRAW_MODEL) != 0) {
            character->ClearFlags(SDK::CS_FLAG_DRAW_MODEL);
        }
        if ((flags & SDK::CS_FLAG_UPDATE_ALWAYS) == 0) {
            character->AddFlags(SDK::CS_FLAG_UPDATE_ALWAYS);
        }
    }

    bool Human::FitDisguiseCollider(SDK::IEntity *entity) {
        auto *physics = entity->GetPhysics();
        if (physics == nullptr) {
            return false;
        }

        // The mesh's own box, not the entity's: KCD2 still counts the hidden
        // character towards the entity bounds. The slot sits at the entity's
        // origin, unscaled, so the box is already in the body's space.
        SDK::IStatObj *mesh = entity->GetStatObj(_disguiseSlot | kActualSlot);
        if (mesh == nullptr) {
            return false;
        }
        const SDK::AABB bounds = mesh->GetAABB();
        if (!(bounds.min.x < bounds.max.x && bounds.min.y < bounds.max.y && bounds.min.z < bounds.max.z)) {
            return false;
        }

        if (!_ownDimensionsSaved) {
            if (physics->GetParams(&_ownDimensions) == 0) {
                return false;
            }
            _ownDimensionsSaved = true;
        }

        // The body's own gap under its cylinder is what lets it step up a
        // stair, so the mesh's cylinder keeps it and starts there.
        const bool capsule    = _ownDimensions.bUseCapsule > 0;
        const float ownBottom = _ownDimensions.heightCollider - _ownDimensions.sizeCollider.z - (capsule ? _ownDimensions.sizeCollider.x : 0.0F);
        const float clearance = std::clamp(ownBottom, 0.0F, kMaxDisguiseClearance);
        const float top       = std::max(bounds.max.z, clearance + kMinDisguiseHeight);
        const float halfSpan  = (top - clearance) * 0.5F;
        const float radius    = std::clamp(std::max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y) * 0.5F, kMinDisguiseRadius, std::min(kMaxDisguiseRadius, halfSpan));

        SDK::pe_player_dimensions wanted;
        wanted.sizeCollider   = SDK::Vec3 {radius, radius, capsule ? std::max(halfSpan - radius, 0.01F) : halfSpan};
        wanted.heightCollider = clearance + halfSpan;
        if (physics->SetParams(&wanted) == 0) {
            // The cylinder would overlap the world and could not be pushed out
            // of it. Asked once: the mesh still shows, on the body's own size.
            Framework::Logging::GetLogger(kLogger)->warn("'{}' could not be given a collider the size of '{}'; it keeps its own", nickname, _appliedDisguise.model);
            _disguiseColliderRefused = true;
            return true;
        }
        if (physics->GetParams(&_disguiseDimensions) == 0) {
            _disguiseDimensions = wanted;
        }
        return true;
    }

    void Human::WithdrawDisguise(SDK::IEntity *entity) {
        // Only ever the mesh: freeing a slot that holds a character destroys it
        // under the Mannequin contexts that cached its skeleton.
        if (_disguiseSlot > kCharacterSlot && entity->GetCharacter(_disguiseSlot) == nullptr) {
            entity->FreeSlot(_disguiseSlot);
        }
        _drawHeldByDisguise = false;
        ReleaseCharacterDraw(entity);
        if (_ownDimensionsSaved && _disguiseColliderFitted && !_disguiseColliderRefused) {
            if (auto *physics = entity->GetPhysics()) {
                physics->SetParams(&_ownDimensions);
            }
        }
        const SDK::EntityId entityId = _disguiseEntityId;
        ForgetDisguise();
        _disguiseEntityId = entityId;
    }

    void Human::ForgetDisguise() {
        _appliedDisguise         = {};
        _disguiseEntityId        = 0;
        _disguiseSlot            = -1;
        _drawHeldByDisguise      = false;
        _ownDimensionsSaved      = false;
        _disguiseColliderFitted  = false;
        _disguiseColliderRefused = false;

        // The character the gate holds is deliberately not dropped here: a
        // hidden body still wants it held, and a stale pointer is harmless
        // because every use tests it against the entity's live character.
    }

    void Human::ReleaseCharacterDraw(SDK::IEntity *entity) {
        if (_drawHeldByDisguise || _drawHeldByHide) {
            return;
        }
        // Only a character that is still this body's, and only what was taken
        // from it: a body that was not drawing before, or was already
        // animating while hidden, is left that way.
        if (_suppressedCharacter == nullptr || entity->GetCharacter(kCharacterSlot) != _suppressedCharacter) {
            return;
        }
        if (_suppressedCharacterDrew) {
            _suppressedCharacter->AddFlags(SDK::CS_FLAG_DRAW_MODEL);
        }
        if (!_suppressedCharacterUpdatedAlways) {
            _suppressedCharacter->ClearFlags(SDK::CS_FLAG_UPDATE_ALWAYS);
        }
        _suppressedCharacter              = nullptr;
        _suppressedCharacterDrew          = false;
        _suppressedCharacterUpdatedAlways = false;
    }

    void Human::AdoptVisibility(bool visible) {
        if (!IsOwner() || bodyHidden == !visible) {
            return;
        }
        // Published at once, as a disguise is: CaptureLocalState puts it on
        // the local body on this same frame.
        bodyHidden = !visible;
        Framework::Logging::GetLogger(kLogger)->info("Body is now {}", bodyHidden ? "hidden" : "drawn");
    }

    void Human::ApplyBodyVisibility(SDK::IEntity *entity, SDK::wh::entitymodule::C_Human *human) {
        // How often the carried half is re-stated while hidden. It is not
        // free -- a deferred engine-lock call and a walk of every socket --
        // and what it catches is a weapon drawn or holstered since, which is
        // a person's action rather than a frame's.
        constexpr MafiaNet::Time kCarriedHoldIntervalMs = 500;

        if (entity->GetId() != _visibilityEntityId) {
            // A different entity is a different body, which starts drawn, and
            // the slots the last one hid belong to sockets that are gone.
            _hiddenCarriedSlots.clear();
            _visibilityEntityId = entity->GetId();
            _appliedBodyHidden  = false;
            _carriedHeldAt      = 0;
        }

        if (bodyHidden != _appliedBodyHidden) {
            // Giving a body back its drawing is one pass and has to be whole:
            // a body that cannot be reached yet keeps the hide and is asked
            // again next frame, rather than being left half shown.
            if (!bodyHidden && human == nullptr) {
                return;
            }
            _appliedBodyHidden = bodyHidden;
            _drawHeldByHide    = bodyHidden;
            _carriedHeldAt     = 0;
            if (!bodyHidden) {
                ReleaseCharacterDraw(entity);
                ShowDisguiseSlot(entity);
                ShowCarriedEntities();
                human->HideAllAttachments(false);
            }
        }

        if (!bodyHidden) {
            return;
        }

        // Held rather than set once, for the reason ApplyDisguise is: a
        // re-dress builds a new character in slot 0 drawing again.
        SuppressCharacterDraw(entity);
        HoldDisguiseSlotHidden(entity);

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_carriedHeldAt == 0 || now >= _carriedHeldAt + kCarriedHoldIntervalMs) {
            _carriedHeldAt = now;
            // The game's own call for what a body carries, and the one it
            // makes for itself when an entity of its own is hidden.
            if (human != nullptr) {
                human->HideAllAttachments(true);
            }
            HoldCarriedEntitiesHidden(entity);
        }
    }

    void Human::HoldDisguiseSlotHidden(SDK::IEntity *entity) {
        if (_disguiseSlot <= kCharacterSlot) {
            return;
        }
        const std::uint32_t flags = entity->GetSlotFlags(_disguiseSlot);
        if ((flags & SDK::ENTITY_SLOT_RENDER) != 0) {
            entity->SetSlotFlags(_disguiseSlot, flags & ~static_cast<std::uint32_t>(SDK::ENTITY_SLOT_RENDER));
        }
    }

    void Human::ShowDisguiseSlot(SDK::IEntity *entity) {
        if (_disguiseSlot <= kCharacterSlot || _disguiseSlot >= entity->GetSlotCount()) {
            return;
        }
        entity->SetSlotFlags(_disguiseSlot, entity->GetSlotFlags(_disguiseSlot) | static_cast<std::uint32_t>(SDK::ENTITY_SLOT_RENDER));
    }

    void Human::HoldCarriedEntitiesHidden(SDK::IEntity *entity) {
        auto *entitySystem                 = SDK::IEntitySystem::GetInstance();
        SDK::ICharacterInstance *character  = entity->GetCharacter(kCharacterSlot);
        SDK::IAttachmentManager *attachments = character != nullptr ? character->GetIAttachmentManager() : nullptr;
        if (entitySystem == nullptr || attachments == nullptr) {
            return;
        }

        // Only the sockets holding an entity are of any interest: everything
        // else draws through the character, whose gate is already shut.
        std::vector<SDK::EntityId> carried;
        const int count = attachments->GetAttachmentCount();
        for (int index = 0; index < count; ++index) {
            SDK::IAttachment *socket       = attachments->GetInterfaceByIndex(static_cast<std::uint32_t>(index));
            SDK::IAttachmentObject *payload = socket != nullptr ? socket->GetIAttachmentObject() : nullptr;
            if (payload == nullptr || payload->GetAttachmentType() != SDK::IAttachmentObject::EType::Entity) {
                continue;
            }
            const SDK::EntityId carriedId = static_cast<SDK::CEntityAttachment *>(payload)->GetEntityId();
            SDK::IEntity *carriedEntity   = carriedId != 0 ? entitySystem->GetEntity(carriedId) : nullptr;
            if (carriedEntity == nullptr) {
                continue;
            }
            carried.push_back(carriedId);

            const int slots = carriedEntity->GetSlotCount();
            for (int slot = 0; slot < slots; ++slot) {
                const std::uint32_t flags = carriedEntity->GetSlotFlags(slot);
                if ((flags & SDK::ENTITY_SLOT_RENDER) == 0) {
                    // Either the game is not drawing it or this already took
                    // it, which is also what stops a second pass recording it.
                    continue;
                }
                carriedEntity->SetSlotFlags(slot, flags & ~static_cast<std::uint32_t>(SDK::ENTITY_SLOT_RENDER));
                _hiddenCarriedSlots.push_back(CarriedSlot {carriedId, slot});
            }
        }

        // What left the body while it was hidden draws again where it landed:
        // a torch dropped by an invisible player is still a torch.
        for (auto held = _hiddenCarriedSlots.begin(); held != _hiddenCarriedSlots.end();) {
            if (std::find(carried.begin(), carried.end(), held->entityId) != carried.end()) {
                ++held;
                continue;
            }
            if (SDK::IEntity *left = entitySystem->GetEntity(held->entityId); left != nullptr && held->slot < left->GetSlotCount()) {
                left->SetSlotFlags(held->slot, left->GetSlotFlags(held->slot) | static_cast<std::uint32_t>(SDK::ENTITY_SLOT_RENDER));
            }
            held = _hiddenCarriedSlots.erase(held);
        }
    }

    void Human::ShowCarriedEntities() {
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            for (const CarriedSlot &held : _hiddenCarriedSlots) {
                // An item destroyed or re-slotted while hidden has nothing to
                // hand back; the slot count is what says so.
                if (SDK::IEntity *carried = entitySystem->GetEntity(held.entityId); carried != nullptr && held.slot < carried->GetSlotCount()) {
                    carried->SetSlotFlags(held.slot, carried->GetSlotFlags(held.slot) | static_cast<std::uint32_t>(SDK::ENTITY_SLOT_RENDER));
                }
            }
        }
        _hiddenCarriedSlots.clear();
    }
} // namespace KCDC::Features::Player
