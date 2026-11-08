#include "features/npc/npc.h"

#include "core/obfuscation.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_locomotion_service.h"
#include "features/npc/npc_service.h"
#include "features/player/player.h"
#include "features/social/social_emote_service.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/combatmodule/c_combat_input_class_database.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "shared/features/inventory/inventory_sync.h"
#include "shared/features/npc/npc_inventory_message.h"
#include "shared/game/equipment_catalog.h"
#include "shared/game/item_classes.h"

#include "core/application.h"

#include "shared/features/npc/npc_rpc.h"

#include <networking/network_peer.h>

#include "common/actor_dressing.h"
#include "common/module_support.h"
#include "common/vector_math.h"
#include "common/spawn/spawn_budget.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/c_game_object.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor_shooting_expansion.h"
#include "sdk/wh/entitymodule/c_actor_speak_man.h"
#include "sdk/wh/entitymodule/c_anim_action_ai_movement.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_ragdoll_manager.h"
#include "sdk/wh/entitymodule/s_speak_data.h"
#include "sdk/wh/rpgmodule/i_soul_system.h"
#include "sdk/wh/rpgmodule/s_character_body_description.h"
#include "sdk/wh/xgenaimodule/c_npc_manager.h"
#include "sdk/wh/xgenaimodule/send_message_to_wuid.h"
#include "shared/game/missile_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace KCDC::Features::Npc {
    namespace {
        constexpr const char *kLogger = "Npc";

        /** How far above and below a point the ground is looked for. */
        constexpr float kGroundProbeUp   = 2.0F;
        constexpr float kGroundProbeDown = 6.0F;

        /** How fast a kinematic body turns towards where it is going, radians a second. */
        constexpr float kTurnRateRadPerSecond = 6.0F;

        /** Cap on one simulation step, so a hitch cannot teleport a body. */
        constexpr float kMaxStepSeconds = 0.25F;

        /**
         * How long a released brain has to take a native flee's escape
         * before the flee fails. A bound, not a measurement: the mailbox
         * appears on the switch tree's first tick, so this only ends the wait
         * for a brain that never takes it.
         */
        constexpr MafiaNet::Time kEscapeHandOverMs = 5000;

        /** The physics profiles `C_RagdollManager` understands. */
    } // namespace

    Npc::~Npc() {
        if (_localPreview) {
            StopLocalPreview(false);
        }
        ReleaseSpawnTicket();
    }

    void Npc::StopLocalPreview(bool worldGone) {
        if (!_localPreview) {
            return;
        }
        NpcService::Get().SetPreviewBody(0, nullptr);
        if (worldGone) {
            Unbind();
        }
        else {
            Despawn();
        }
        _localPreview = false;
    }

    // --- Replica3 ------------------------------------------------------------

    void Npc::OnConstructed() {
        Framework::Logging::GetLogger(kLogger)->info("NPC replica constructed: networkId={}, soul='{}', level={}, simulating={}", GetNetworkID(), soulGuid, levelGuid, IsSimulating());
        _dirty.store(true, std::memory_order_release);
    }

    void Npc::NoteLookChanges() {
        // The clothes and the face are compared against what was applied rather
        // than against the previous message: a state update that changed
        // something else must not re-dress the body, and one that did change
        // them must, even if the body was not ready last time.
        if (_appliedEquipment != equipment) {
            _equipmentDirty = true;
        }
        if (!(_appliedAppearance == appearance)) {
            _appearanceDirty   = true;
            _appearanceRefused = false;
        }
    }

    void Npc::OnDeserialized(bool transformUpdated) {
        NoteLookChanges();

        // An observer is moved by the poses it interpolates; a teleport it saw
        // as an observer is not one to replay when it becomes the simulator.
        _appliedTeleportSequence = teleportSequence;

        // A simulator draws the body its own engine is moving; a pose it sent a
        // moment ago is not something to interpolate towards.
        if (!transformUpdated || IsSimulating()) {
            return;
        }
        if (!transformReady) {
            return;
        }
        // A pose for a body that has been replaced says where the old one was.
        if (poseEpoch != spawnEpoch) {
            return;
        }

        // Tags taken here rather than at draw time: the state channel was
        // deserialized in this same call, so this is the one moment the tags and
        // the transform are known to belong together.
        KCDC::Common::Replication::PoseSnapshot pose;
        pose.position     = position;
        pose.velocity     = velocity;
        pose.rotation     = rotation;
        pose.moveSpeedTag = moveSpeedTag;
        pose.moveDirTag   = moveDirTag;
        pose.stanceTag    = stanceTag;
        pose.inAir        = inAir;
        _snapshots.Push(pose, lastUpdateTime);
    }

    void Npc::OnStateForced() {
        // The server has spoken to the client running this body -- the one peer
        // the field channel never reaches. Everything it decided is in the
        // forced block, and the intent among it, so the runner re-derives from
        // the revision rather than continuing what it was doing.
        _dirty.store(true, std::memory_order_release);

        // The forced block carries the look too, and this is the only way it
        // reaches the body's own runner: a `wear()` on a simulated NPC used to
        // re-dress every copy but the one the simulator draws.
        NoteLookChanges();

        // The block also carries the transform, which is how a scripted
        // teleport reaches a simulator at all -- but every intent revision
        // carries it too, as the server's stale copy of where this client last
        // put the body. Only a new teleport sequence is a move. Taken on the
        // next step rather than here: this runs on the peer pump, where the
        // entity system is not safe to touch.
        if (teleportSequence != _appliedTeleportSequence) {
            _appliedTeleportSequence = teleportSequence;
            _forcedPosePending       = true;
        }
    }

    void Npc::DeallocReplica(MafiaNet::Connection_RM3 *) {
        Despawn();
        delete this;
    }

    SDK::wh::entitymodule::C_Actor *Npc::ResolveActor() const {
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        return _entityId != 0 && actorSystem != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(_entityId)) : nullptr;
    }

    // --- Per-frame -----------------------------------------------------------

    void Npc::Frame() {
        // The first spawn must already know its stock: dressing after the
        // initial animation binding can replace the bound character.
        if (!_localPreview && _seenInventoryState != inventoryState) {
            _seenInventoryState = inventoryState;
            AcceptInventory(nlohmann::json::parse(inventoryState, nullptr, false));
        }
        // A body made for a previous incarnation is not this NPC's body: a
        // revive bumps the epoch, and the corpse is replaced rather than
        // stood up.
        if (_entityId != 0 && _bodyEpoch != spawnEpoch) {
            Despawn();
        }

        if (_entityId == 0) {
            if (IsSpawned()) {
                RequestSpawn();
            }
            else if (!BindLevelBody()) {
                return;
            }
            if (_entityId == 0) {
                return;
            }
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(_entityId) : nullptr;
        if (entity == nullptr) {
            // The body went with a level change; the next tick makes another.
            // What was buffered describes a world that is no longer loaded.
            Unbind();
            return;
        }

        auto *actor = ResolveActor();
        if (actor == nullptr) {
            return;
        }

        if (!_bodyReady) {
            const LookResult look = ApplyLook();
            if (look == LookResult::Retry) {
                return;
            }
            if (look == LookResult::Refused) {
                Despawn();
                _spawnRefused = true;
                Framework::Logging::GetLogger(kLogger)->error("Rolled back the body of NPC {}: the engine refused to dress it", GetNetworkID());
                return;
            }
            _bodyReady = true;
        }

        // Both of these are cheap once satisfied and neither can be done before
        // the model is up, so they are asked every frame until they take.
        if (!_broughtUp) {
            _broughtUp = KCDC::Common::Replication::BodyLife::BringUp(actor, entity);
        }
        KCDC::Common::Replication::BodyLife::Get().Bind(actor->GetSoul(), this);
        ApplyHarvestedAppearance(actor);
        if (!RefreshAnimationBinding(actor)) {
            // The body is being rebuilt; `entity` and `actor` are already gone.
            return;
        }

        // These two are what a server-driven look change reaches the body
        // through. Either may replace the character instance, and neither can
        // tell whether it did -- so neither is guarded here. `RefreshAnimationBinding`
        // is, on the next frame: it is the one place that can see the instance
        // change, and it rebuilds the body rather than rebinding it live.
        if (_equipmentDirty) {
            ApplyOutfit(actor);
        }
        if (!_localPreview) {
            if (_stockReady) {
                ApplyInventory();
            }
            if (IsSimulating() && (!_stockReady || !_nativeStockKnown)) {
                NpcInventoryModule::Get().Seed(*this);
            }
        }
        if (_appearanceDirty && !_appearanceRefused) {
            ApplyAppearance(actor);
        }

        if (!HasAttackOrder()) {
            StopCombat(actor);
        }
        if (!FleesNatively()) {
            StopEscape();
        }
        ReconcileName(actor->GetSoul());
        ApplyVitals(actor);

        // Until Mannequin is up on this body, it is placed but never animated:
        // the override is consumed on a job thread and names a fragment-table
        // index, and a body with no table dies there rather than here.
        if (!IsPresentable(actor)) {
            if (!IsSimulating() && transformReady && KCDC::Common::IsFinite(position)) {
                KCDC::Common::PlaceEntity(entity, position, rotation);
            }
            return;
        }

        // A dead body is a corpse: the engine's own death has it, and nothing
        // here should be posing it or steering it -- an observer's locomotion
        // override included, which the tag detour states every frame for as
        // long as it is published. Its NoMovementOverride forces the movement
        // control method to Entity (`UpdateMCMComponent`, 0x18083B7BC) over
        // the Animation the red deer's, the hare's and most human death
        // fragments ask for, so the fall the simulator's body travels before
        // its Ragdoll clip -- up to 5.9 m for a red deer killed at a gallop --
        // would play on the spot here.
        if (!alive) {
            ReleaseSteering(actor);
            WithdrawLocomotion();
            return;
        }

        if (IsSimulating()) {
            // Nothing is forced here. `C_ActorMovementController::Update`
            // (0x1804B8E88) derives the desired velocity from the move target
            // and the desired speed on the request itself -- proven against
            // CryEngine's CPlayerMovementController::Update, down to the
            // `(distance - 0.2f) / 1.8f` anim-target blend -- so the body is
            // steered by the controller and needs no help choosing a state. Forcing one while writing no tags picks a fragment with no
            // pace, and under an animation-driven MCM the clip is what travels.
            WithdrawLocomotion();
            Simulate(actor, entity);
            return;
        }

        ReleaseSteering(actor);
        PresentRemote(actor, entity);
    }

    void Npc::Unbind() {
        _inventoryProjection.Clear();
        _stockApplied = false;
        _stockRetry.Reset();
        if (_localPreview) {
            NpcService::Get().SetPreviewBody(0, nullptr);
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetPlayerMissileService().ForgetActor(GetNetworkID());
        }
        KCDC::Common::Replication::BodyLife::Get().Unbind(this);
        _offenseToken   = 0;
        _defenseToken   = 0;
        _guardToken     = 0;
        _combatTarget   = 0;
        _bowReleaseAt   = 0;
        _bowReadyBy     = 0;
        _escapeRevision = 0;
        _escapeDueBy    = 0;
        _entityId       = 0;
        _bodyReady      = false;
        _npcRecord      = nullptr;
        _broughtUp      = false;
        _boundCharacter = nullptr;
        _appliedHarvested.reset();
        _equipmentDirty = true;
        _appliedEquipment.clear();
        _appliedAppearance = {};
        _appearanceDirty   = true;
        _appearanceRefused = false;
        _appliedHealth     = -1.0F;
        _steering          = false;
        _heldInPlace       = false;
        _reportedOnce      = false;
        _movementProgress.Reset();
        _snapshots.Clear();
    }

    // --- Body lifecycle ------------------------------------------------------

    void Npc::RequestSpawn() {
        if (_spawnRefused || _spawnTicket != nullptr || !transformReady) {
            return;
        }

        const auto now = MafiaNet::GetTime();
        if (now < _nextSpawnAttempt) {
            return;
        }
        _nextSpawnAttempt = now + KCDC::Common::Spawn::SpawnRetryDelayMs(GetNetworkID());

        if (!KCDC::Common::IsFinite(position)) {
            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cannot spawn NPC {}: position is not finite", GetNetworkID());
            return;
        }

        // Grounded before the body is made: the server has no engine and placed
        // this NPC at whatever Z the caller passed, which floats or buries it
        // wherever the ground is not level with that.
        glm::vec3 placement = position;
        float ground        = placement.z;
        if (GroundAt(placement, ground)) {
            placement.z = ground;
        }

        KCDC::Common::Spawn::SpawnRequest request;
        request.actorClass = SpawnClass();
        request.soulGuid   = soulGuid;
        if (IsAnimal()) {
            // Server orders replace the level's home/schedule links.
            request.properties.emplace_back("bWH_RequiresHome", false);
        }
        request.world      = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(placement), KCDC::Common::ToNative(rotation)};

        _spawnTicket = KCDC::Core::gApplication->GetActorSpawner().Request(request, [this](KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
            OnSpawned(ticket, spawned);
        });
    }

    void Npc::OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
        // The ticket has run either way, so it never fires again.
        _spawnTicket = nullptr;

        if (!spawned) {
            if (KCDC::Common::Spawn::IsRetryableSpawnResult(ticket.GetResult())) {
                return;
            }
            _spawnRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Could not spawn NPC {} (soul '{}'): {}", GetNetworkID(), soulGuid, KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
            return;
        }

        _entityId  = ticket.GetEntityId();
        _bodyEpoch = spawnEpoch;
        _bodyReady = false;

        // Kept so the brain detour can recognise this body's mind: a brain knows
        // its `C_NPC` (brain+0x08) and nothing else about the entity.
        auto *manager = SDK::wh::xgenaimodule::C_NPCManager::GetInstance();
        _npcRecord    = manager != nullptr ? manager->GetNPCByEntityId(_entityId) : nullptr;
        if (_localPreview) {
            NpcService::Get().SetPreviewBody(_entityId, _npcRecord);
        }
        Framework::Logging::GetLogger(kLogger)->info("Spawned NPC {} (soul '{}') as entity {}", GetNetworkID(), soulGuid, _entityId);

        // Dressed in this tick, not later. Dressing replaces the character
        // instance and `RefreshAnimationBinding` repoints Mannequin at the new
        // one; the engine does that holding a `C_ScopedEngineLock` and this runs
        // out of `PostUpdate`, which holds nothing. The spawner calls this back
        // synchronously out of `SpawnNow`, so no engine update has run yet and
        // nothing can be animating the body. Doing it once the body has settled
        // races the animation job and aborts inside `CActionScope`.
        const LookResult look = ApplyLook();
        if (look == LookResult::Applied) {
            _bodyReady = true;
            if (auto *actor = ResolveActor()) {
                // The first binding, and the only one that is free: nothing has
                // animated this body yet. `_boundCharacter` is null here, so the
                // rebuild branch cannot be taken and the answer cannot be false.
                RefreshAnimationBinding(actor);
            }
            return;
        }

        // A body that could not be dressed here is rolled back rather than left
        // standing undressed, because the only way to dress it later is the way
        // that crashes. Retryable means the whole spawn is retried.
        Despawn();
        _spawnRefused = look != LookResult::Retry;
        if (_spawnRefused) {
            Framework::Logging::GetLogger(kLogger)->error("Rolled back NPC {}: the engine refused to dress it in its spawn tick", GetNetworkID());
        }
        else {
            Framework::Logging::GetLogger(kLogger)->warn("NPC {} was not ready to be dressed in its spawn tick; respawning it", GetNetworkID());
        }
    }

    bool Npc::BindLevelBody() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr || levelGuid == 0 || _spawnRefused) {
            return false;
        }

        // The level's own body, by the guid every machine agrees on. It only
        // exists once its layer has streamed in, so this is asked again every
        // frame until it does rather than refused.
        const SDK::EntityId entityId = entitySystem->FindEntityByGuid(levelGuid);
        if (entityId == 0) {
            return false;
        }

        auto *actorSystem = SDK::IActorSystem::GetInstance();
        if (actorSystem == nullptr || actorSystem->GetActor(entityId) == nullptr) {
            // The guid names something that is not an actor. Nothing retrying
            // can fix that, so say so once and stop.
            if (!_spawnRefused) {
                _spawnRefused = true;
                Framework::Logging::GetLogger(kLogger)->error("NPC {} adopts level entity {:#x}, which is not an actor", GetNetworkID(), levelGuid);
            }
            return false;
        }

        _entityId  = entityId;
        _bodyEpoch = spawnEpoch;
        _bodyReady = false;
        Framework::Logging::GetLogger(kLogger)->info("NPC {} adopted level body {:#x} (entity {})", GetNetworkID(), levelGuid, _entityId);
        return true;
    }

    void Npc::ReleaseSpawnTicket() {
        if (_spawnTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().Return(_spawnTicket);
        }
        _spawnTicket = nullptr;
    }

    void Npc::Despawn() {
        // Dropped first, so a body that was still queued is never made for an
        // NPC that has already gone.
        ReleaseSpawnTicket();

        const SDK::EntityId entityId = _entityId;
        const bool spawnedBody       = IsSpawned();

        // Resolved before the unbind, which is what clears the id this resolves
        // through: the body has to be stood down and, if it is the level's, handed
        // back, and after the unbind there is nothing left to do either with.
        SDK::wh::entitymodule::C_Actor *actor = ResolveActor();

        // A corpse is stood up before it is retired; see `PrepareForRemoval`.
        // This is the same crash a respawn once caused on every observer.
        StopCombat(actor);
        NpcInventoryModule::Get().ReleaseBody(*this);
        KCDC::Common::Replication::BodyLife::PrepareForRemoval(actor);
        Unbind();

        if (entityId == 0) {
            return;
        }

        // A level body is given back rather than removed: the mod did not make
        // it, and the level expects it to still be there.
        if (!spawnedBody) {
            if (actor != nullptr) {
                if (auto *animated = actor->GetAnimatedCharacterRuntime()) {
                    animated->SetNoMovementOverride(false);
                }
            }
            Framework::Logging::GetLogger(kLogger)->info("Released level body of NPC {} (entity {})", GetNetworkID(), entityId);
            return;
        }

        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetActorSpawner().DespawnNow(entityId);
            Framework::Logging::GetLogger(kLogger)->info("Despawned NPC {} (entity {})", GetNetworkID(), entityId);
        }
    }

    Npc::LookResult Npc::ApplyLook() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(_entityId) : nullptr;
        auto *actor        = ResolveActor();
        if (entity == nullptr || actor == nullptr) {
            return LookResult::Refused;
        }

        // The body keeps the brain registration gave it: the pace it walks at has
        // one writer, `C_ActorMovementController::Update`, whose AI branch
        // (0x1804BABAC) needs the AI movement state a mindless body does not have.
        // Installing `C_EmptySUBB` here left the pace at zero and the body
        // standing. Reactions are refused in `npc_hooks.cpp` instead.
        auto *soulSystem = SDK::wh::rpgmodule::I_SoulSystem::GetInstance();
        if (soulSystem == nullptr) {
            return LookResult::Retry;
        }

        // This accessor is what guarantees initialization; the cached pointer
        // cannot tell an uninitialised soul from a live one.
        auto *soul = SDK::wh::rpgmodule::GetOrCreateSoul(soulSystem, entity);
        if (soul == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("NPC {} entity {} has no initialized soul", GetNetworkID(), _entityId);
            return LookResult::Refused;
        }

        if (IsAnimal()) {
            // Animal classes load their own CDF. Human equipment and body-part
            // overrides must never rebuild it with a humanoid skeleton.
            actor->RequestPutAway(false, true);
            if (IsSpawned() && !KCDC::Common::Spawn::ActorSpawner::EnsureClassCharacter(entity)) {
                return LookResult::Refused;
            }
            _equipmentDirty  = false;
            _appearanceDirty = false;
            RefreshAnimationBinding(actor);
            return LookResult::Applied;
        }

        actor->RefreshClothingSupport();

        // Wake before the equipment listener is made. RequestPutAway keeps the
        // wanted/applied bits and the pending-list membership consistent, which
        // is why it is never SetPutAway. The wake is also what loads a spawned
        // body's character; an adopted one is already wearing one.
        actor->RequestPutAway(false, true);
        if (IsSpawned() && !KCDC::Common::Spawn::ActorSpawner::EnsureClassCharacter(entity)) {
            Framework::Logging::GetLogger(kLogger)->error("NPC {} entity {} loaded no character model", GetNetworkID(), _entityId);
            return LookResult::Refused;
        }

        actor->EnsureEquipmentHandler();
        actor->RebuildClothing();

        // MakeLookAsSoul pairs the equipment listener with the source actor as
        // well as writing the layer this then overwrites, so it goes first
        // rather than instead.
        actor->MakeLookAsSoul(soul);
        ApplyAppearance(actor);
        ApplyOutfit(actor);
        if (_stockReady && !_localPreview && !ApplyInventory()) {
            return LookResult::Refused;
        }

        ApplyHarvestedAppearance(actor);
        // Dressing can replace the character instance.
        RefreshAnimationBinding(actor);
        return LookResult::Applied;
    }

    void Npc::ApplyHarvestedAppearance(SDK::wh::entitymodule::C_Actor *actor) {
        if (_localPreview || !_stockReady || !_nativeStockKnown || !_harvestable) {
            return;
        }
        const bool harvested = _stockInteraction.harvested;
        if (_appliedHarvested == harvested) {
            return;
        }
        auto *soul = actor->GetSoul();
        if (soul == nullptr) {
            return;
        }
        if (harvested) {
            // Native Butcher changes part 4 and rebuilds clothing/physics.
            // It never generates items. Classes without a skinned model
            // return false and legitimately keep their original appearance.
            soul->Butcher();
        }
        else if (soul->GetActiveStateStorage()->IsButchered()) {
            soul->ClearOverrideBodyDescriptionPart(SDK::wh::rpgmodule::S_CharacterBodyDescription::kUnderwear);
            soul->GetActiveStateStorage()->ClearButchered();
            soul->RefreshAppearance();
            actor->GetRagdollManager()->Rephysicalize();
        }
        _appliedHarvested = harvested;
    }

    bool Npc::IsPresentable(SDK::wh::entitymodule::C_Actor *actor) const {
        if (actor == nullptr || !actor->IsSettled()) {
            return false;
        }
        // The bound third-person character is the proof that Mannequin has
        // something to play on: the emote path refuses to queue an action
        // without it for the same reason.
        auto *animated = actor->GetAnimatedCharacterRuntime();
        return _boundCharacter != nullptr && animated != nullptr && animated->GetActionController() != nullptr && animated->GetBoundCharacter3P() != nullptr;
    }

    bool Npc::RefreshAnimationBinding(SDK::wh::entitymodule::C_Actor *actor) {
        auto *entity = actor->GetEntity();
        if (entity == nullptr) {
            return true;
        }

        auto *character = entity->GetCharacter(0);
        auto *animated  = actor->GetAnimatedCharacterRuntime();
        if (character == nullptr || character == _boundCharacter || animated == nullptr) {
            return true;
        }

        // A body that had an instance and now has a different one was re-dressed
        // while it was live. See the header: rebinding here is the race, so the
        // body goes and comes back dressed in its own spawn tick instead. An
        // adopted level body has no spawn tick to go back to and takes the
        // rebind, which is the lesser of the two bad options for it.
        if (_boundCharacter != nullptr && IsSpawned()) {
            Framework::Logging::GetLogger(kLogger)->info("NPC {} was re-dressed while live; rebuilding the body rather than rebinding it under the animation job", GetNetworkID());
            Despawn();
            return false;
        }

        animated->RefreshCharacterBindings();
        _boundCharacter = character;
        return true;
    }

    std::string Npc::LocalizationKey() {
        return "kcdc_npc_" + std::to_string(GetNetworkID());
    }

    void Npc::ReconcileName(SDK::wh::rpgmodule::C_Soul *soul) {
        if (soul == nullptr || name.empty()) {
            return;
        }

        const std::string key = LocalizationKey();
        const char *current   = soul->GetUIName();
        if (current != nullptr && key == current) {
            return;
        }

        soul->SetUIName(key.c_str());
        Framework::Logging::GetLogger(kLogger)->info("NPC {} is named '{}' in the game's own UI, under key '{}'", GetNetworkID(), name, key);
    }

    void Npc::ApplyOutfit(SDK::wh::entitymodule::C_Actor *actor) {
        if (IsAnimal() || (_stockReady && !_localPreview)) {
            _equipmentDirty = false;
            _appliedEquipment = equipment;
            return;
        }
        // An NPC the server gave no clothes wears its soul's own, so nothing
        // specified means taking ours off rather than stripping the body.
        KCDC::Common::DressOptions options;
        options.emptyRestoresOwnOutfit = true;
        const KCDC::Common::DressResult result = KCDC::Common::ApplyActorEquipment(actor, equipment, options);
        if (!result.ready) {
            // Keep the flag: the body was not ready, not wrongly dressed.
            return;
        }
        if (!result.dressed) {
            // Not retried: the same list would be refused the same way.
            Framework::Logging::GetLogger(kLogger)->error("NPC {} could not be dressed in its {} piece(s); it keeps what it had on", GetNetworkID(), result.armorPieces);
        }
        _appliedEquipment = equipment;
        _equipmentDirty   = false;
    }

    SDK::wh::entitymodule::C_Inventory *Npc::ProjectedInventory() const {
        auto *actor     = ResolveActor();
        auto *inventory = actor != nullptr ? actor->GetInventoryRuntime() : nullptr;
        return _inventoryProjection.IsBoundTo(inventory) ? inventory : nullptr;
    }

    bool Npc::AcceptInventory(const nlohmann::json &snapshot) {
        std::uint64_t state = 0, revision = 0;
        KCDC::Shared::NpcInventory::InteractionState interaction;
        if (!interaction.Read(snapshot) || !snapshot.contains("harvestable") || !snapshot.at("harvestable").is_boolean() || (interaction.harvesting && !snapshot.at("harvestable").get<bool>()) || !KCDC::Shared::NpcInventory::Unsigned(snapshot, "state", state)
            || state <= _stockState || !KCDC::Shared::NpcInventory::Unsigned(snapshot, "revision", revision) || !snapshot.contains("ready") || !snapshot.at("ready").is_boolean() || !snapshot.contains("nativeKnown") || !snapshot.at("nativeKnown").is_boolean()
            || !snapshot.contains("rows") || !snapshot.at("rows").is_array() || snapshot.at("rows").size() > KCDC::Shared::NpcInventory::kMaximumRows || !snapshot.contains("equipped") || !snapshot.at("equipped").is_object()) {
            return false;
        }
        std::map<std::string, nlohmann::json> rows;
        for (const auto &row : snapshot.at("rows")) {
            if (!row.is_object() || !row.contains("id") || !row.at("id").is_string() || !row.contains("item") || !row.contains("amount") || !row.contains("metadata")) {
                return false;
            }
            const std::string id = row.at("id").get<std::string>();
            nlohmann::json projected {{"item", row.at("item")}, {"amount", row.at("amount")}, {"metadata", row.at("metadata")}};
            if (!KCDC::Shared::Inventory::IsSyncRow(id, projected) || !rows.emplace(id, std::move(projected)).second) {
                return false;
            }
        }
        std::map<std::string, std::uint32_t> worn;
        for (auto item = snapshot.at("equipped").begin(); item != snapshot.at("equipped").end(); ++item) {
            if (!item.value().is_number_unsigned() || !rows.contains(item.key()) || item.value().get<std::uint64_t>() > rows.at(item.key()).at("amount").get<std::uint64_t>()) {
                return false;
            }
            if (const auto count = item.value().get<std::uint32_t>(); count != 0) {
                worn.emplace(item.key(), count);
            }
        }
        const bool projectionChanged = rows != _stockRows || worn != _stockEquipped || _stockReady != snapshot.at("ready").get<bool>() || _harvestable != snapshot.at("harvestable").get<bool>() || _stockInteraction.harvested != interaction.harvested;
        _stockRows        = std::move(rows);
        _stockEquipped    = std::move(worn);
        _stockReady       = snapshot.at("ready").get<bool>();
        _nativeStockKnown = snapshot.at("nativeKnown").get<bool>();
        _stockInteraction = interaction;
        _harvestable      = snapshot.at("harvestable").get<bool>();
        _stockState       = state;
        _stockRevision    = revision;
        if (projectionChanged) {
            NotifyNativeInventoryChange();
        }
        return true;
    }

    bool Npc::ReadInitialInventory(nlohmann::json &rows) const {
        auto *actor     = ResolveActor();
        auto *inventory = actor != nullptr ? actor->GetInventoryRuntime() : nullptr;
        auto *manager   = actor != nullptr ? actor->GetEquipmentManagerRuntime() : nullptr;
        if (!_bodyReady || inventory == nullptr || inventory->GetItemCount() > KCDC::Shared::NpcInventory::kMaximumRows) {
            return false;
        }
        rows = nlohmann::json::array();
        for (std::size_t i = 0; i < inventory->GetItemCount(); ++i) {
            auto *item = inventory->GetItemAt(i);
            nlohmann::json row;
            std::string error;
            if (item == nullptr || item->GetAmount() == 0 || !KCDC::Features::Items::NativeInventoryProjection::ReadNativeRow(*item, row, error)) {
                return false;
            }
            bool worn = false;
            if (manager != nullptr) {
                for (std::size_t j = 0; j < manager->GetCarriedCount(); ++j) {
                    worn = worn || manager->GetCarriedAt(j) == item;
                }
            }
            const auto *itemClass = KCDC::Shared::Game::FindItemClass(row.at("item").get_ref<const std::string &>());
            row["equipped"] = worn ? (itemClass->category == "Ammo" ? item->GetAmount() : 1U) : 0U;
            rows.push_back(std::move(row));
        }
        return true;
    }

    bool Npc::ApplyInventory() {
        const auto now = MafiaNet::GetTimeUS() / 1000;
        if (!_stockReady || !_stockRetry.CanAttempt(now)) {
            return _stockApplied;
        }
        auto *actor     = ResolveActor();
        auto *inventory = actor != nullptr ? actor->GetInventoryRuntime() : nullptr;
        auto *manager   = actor != nullptr ? actor->GetEquipmentManagerRuntime() : nullptr;
        if (_stockApplied && _inventoryProjection.IsBoundTo(inventory)) {
            return true;
        }
        // BasicAnimal's fast prompt returns Loot immediately when native
        // stock is nonempty. Prepared parts remain server stock until the
        // accepted harvest; the native inventory represents accessible loot.
        const std::map<std::string, nlohmann::json> sealedRows;
        const std::map<std::string, std::uint32_t> sealedEquipment;
        const auto &rows     = CanBeHarvested() ? sealedRows : _stockRows;
        const auto &equipped = CanBeHarvested() ? sealedEquipment : _stockEquipped;
        if (inventory == nullptr || (manager == nullptr && !equipped.empty())) {
            return false;
        }
        KCDC::Features::Player::PlayerInventoryService::AuthorityProjection applying(KCDC::Core::gApplication->GetPlayerInventoryService());
        std::map<std::string, std::string> unshown;
        std::string error;
        if (!_inventoryProjection.Apply(inventory, rows, unshown, error) || !unshown.empty()) {
            _stockApplied = false;
            _stockRetry.Failed(now);
            Framework::Logging::GetLogger(kLogger)->warn("Body {} inventory projection failed: {} ({} unavailable rows)", GetNetworkID(), error, unshown.size());
            return false;
        }
        if (manager == nullptr) {
            _stockApplied = true;
            _stockRetry.Reset();
            return true;
        }
        std::vector<std::uint64_t> wanted;
        std::vector<std::pair<std::uint32_t, std::string>> ordered;
        for (const auto &[id, count] : equipped) {
            const auto *equipment = KCDC::Shared::Game::FindEquipmentClass(rows.at(id).at("item").get_ref<const std::string &>());
            ordered.emplace_back(equipment != nullptr ? equipment->equipOrder : 0U, id);
        }
        // Row identities carry no dressing order. An underlayer must be on
        // the body before its helmet, outer armor, spurs or caparison.
        std::sort(ordered.begin(), ordered.end());
        for (const auto &[order, id] : ordered) {
            const auto native = _inventoryProjection.NativeIdOf(id);
            if (native != 0 && std::find(wanted.begin(), wanted.end(), native) == wanted.end()) {
                wanted.push_back(native);
            }
        }
        std::vector<std::uint64_t> stale;
        for (std::size_t i = 0; i < manager->GetCarriedCount(); ++i) {
            if (auto *item = manager->GetCarriedAt(i); item != nullptr && std::find(wanted.begin(), wanted.end(), item->GetId()) == wanted.end()) {
                stale.push_back(item->GetId());
            }
        }
        for (const auto id : stale) {
            if (auto *item = inventory->FindOwnedItemById(id)) {
                if (manager->IsInWeaponSlot(item)) {
                    manager->UnequipWeapon(item);
                }
                else {
                    manager->InvokeUnequipItemRaw(item, 1, SDK::wh::framework::WUID::kInvalid);
                }
            }
        }
        for (const auto id : wanted) {
            auto *item = inventory->FindOwnedItemById(id);
            bool worn  = false;
            for (std::size_t i = 0; i < manager->GetCarriedCount(); ++i) {
                worn = worn || manager->GetCarriedAt(i) == item;
            }
            if (item == nullptr || (!worn && !manager->InvokeEquipItemRaw(item))) {
                _stockApplied = false;
                _stockRetry.Failed(now);
                Framework::Logging::GetLogger(kLogger)->warn("Body {} could not equip inventory item {} at revision {}", GetNetworkID(), id, _stockRevision);
                return false;
            }
        }
        // Equipping a later item may displace an earlier one from its slot.
        // Do not expose a screen whose displayed equipment is incomplete.
        for (const auto id : wanted) {
            bool worn = false;
            for (std::size_t i = 0; i < manager->GetCarriedCount(); ++i) {
                if (const auto *item = manager->GetCarriedAt(i)) {
                    worn = worn || item->GetId() == id;
                }
            }
            if (!worn) {
                _stockApplied = false;
                _stockRetry.Failed(now);
                Framework::Logging::GetLogger(kLogger)->warn("Body {} inventory item {} was displaced at revision {}", GetNetworkID(), id, _stockRevision);
                return false;
            }
        }
        _stockApplied = true;
        _stockRetry.Reset();
        return true;
    }

    bool Npc::ApplyAppearance(SDK::wh::entitymodule::C_Actor *actor) {
        if (IsAnimal()) {
            _appearanceDirty = false;
            return appearance.IsEmpty();
        }
        SDK::wh::rpgmodule::I_Soul *soul = actor->GetSoul();
        SDK::IEntity *entity             = actor->GetEntity();
        if (soul == nullptr || entity == nullptr || entity->GetClass() == nullptr) {
            return false;
        }

        const auto refuse = [this]() {
            _appearanceDirty   = false;
            _appearanceRefused = true;
            return false;
        };

        // Checked on the receiver rather than trusted from the server.
        if (!appearance.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Refusing an unknown appearance for NPC {}", GetNetworkID());
            return refuse();
        }

        // Unlike a player's body, an NPC's gender is not negotiable here: it
        // comes from the soul the server chose, and moving a shared soul's
        // archetype would change what every other body spawned from it is. A
        // server that wants a woman spawns a female role.
        //
        // Both ways round, and against the class the body really is -- a
        // level NPC's is the level's, not SpawnClass: the class loaded the
        // skeleton, and parts from the other gender's tree skin against a
        // bind pose that is not theirs.
        const char *bodyClass = entity->GetClass()->GetName();
        const bool femaleBody = std::strcmp(bodyClass, SDK::ActorClassNames::NPCFemale) == 0;
        if (!appearance.IsEmpty() && (appearance.gender == Shared::Game::AppearanceGender::Female) != femaleBody) {
            Framework::Logging::GetLogger(kLogger)->warn("NPC {} was given a {} appearance on a {} body; spawn it as a role of that gender instead", GetNetworkID(), appearance.gender == Shared::Game::AppearanceGender::Female ? "female" : "male", bodyClass);
            return refuse();
        }

        // A beard's .skin loads out of the head's own folder, so one nobody
        // modelled for this face resolves to a file that is not there: no
        // attachment, no error, a body that comes back clean-shaven.
        const std::string &beard = appearance.Part(Shared::Game::AppearancePart::Beard);
        if (!beard.empty()) {
            const std::string &head = appearance.Part(Shared::Game::AppearancePart::Head);
            if (!head.empty() && !Shared::Game::IsBeardOnHead(appearance.gender, head, beard)) {
                Framework::Logging::GetLogger(kLogger)->warn("NPC {} cannot wear beard '{}' on head '{}'", GetNetworkID(), beard, head);
                return refuse();
            }
        }

        // Each write rebuilds the character, so only what moved is touched.
        for (std::size_t part = 0; part < Shared::Game::CharacterAppearance::kPartCount; ++part) {
            const std::string &wanted = appearance.parts[part];
            if (wanted == _appliedAppearance.parts[part]) {
                continue;
            }
            if (wanted.empty()) {
                // Cleared rather than written empty, so the soul's own layers
                // answer that part again.
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

    void Npc::ApplyVitals(SDK::wh::entitymodule::C_Actor *actor) {
        auto *soul = actor->GetSoul();
        if (!actor->IsSettled() || soul == nullptr) {
            return;
        }
        auto &life = KCDC::Common::Replication::BodyLife::Get();

        // The soul is the state: a dead ledger kills it once, the way a blow
        // would, and a living one brings a dead soul back. Nothing is latched
        // beside it to fall out of step.
        if (!alive) {
            if (!soul->IsDead()) {
                life.Kill(actor);
            }
            return;
        }

        const float soulMaximum = soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
        const float fraction    = maxHealth > 0.0F ? std::clamp(health / maxHealth, 0.0F, 1.0F) : 1.0F;
        const float wanted      = fraction * soulMaximum;

        if (soul->IsDead()) {
            life.Revive(actor, wanted > 0.0F ? wanted : soulMaximum);
            _appliedHealth  = health;
            _boundCharacter = nullptr;
            _appliedHarvested.reset();
            _broughtUp      = false;
            RefreshAnimationBinding(actor);
            return;
        }

        // The ledger is in the server's units; the soul's own maximum is the
        // scale it is drawn against, so the fraction is what carries across.
        if (_appliedHealth != health && std::isfinite(wanted) && wanted > 0.0F) {
            life.SetHealth(actor, wanted);
            _appliedHealth = health;
        }
    }

    void Npc::OnLocalBlow(float amount, std::uint8_t) {
        if (_localPreview) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::NpcDamage damage;
        damage.npcNetworkId = GetNetworkID();
        damage.victimEpoch  = spawnEpoch;
        damage.amount       = std::min(amount, Shared::RPC::NpcDamage::kMaxAmount);
        if (damage.IsSane()) {
            net->BroadcastRPC(damage, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    // --- Observer ------------------------------------------------------------

    void Npc::PresentRemote(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity) {
        // Only a fight's two states cost anything here: a body that was never
        // ordered to attack is not given a combat actor it would never use.
        const bool up = IsAttacking() && combatStance && combatStateRevision == intentRevision;
        auto *fighter = up ? actor->GetOrCreateCombatActor() : actor->GetCombatActor();
        if (fighter != nullptr && fighter->IsInitialised() && (up || fighter->IsFightingStanceUp())) {
            if (!up) {
                fighter->RequestGuard(static_cast<SDK::wh::combatmodule::C_CombatActor::GuardRequest>(0));
                fighter->LeaveCombatModeIfActive();
            }
            else {
                if (!fighter->IsFightingStanceUp()) {
                    if (auto *hands = actor->GetItemAttachmentManager()) {
                        hands->DrawWeaponSet(SDK::wh::entitymodule::E_WeaponSet::Primary);
                    }
                    fighter->TryEnterCombatMode();
                }
                fighter->RequestGuard(static_cast<SDK::wh::combatmodule::C_CombatActor::GuardRequest>(combatGuard <= 2 ? combatGuard : 0));
                if (combatZone >= 0 && combatZone < 6 && fighter->GetModel()->GetCombatZone() != combatZone) {
                    fighter->SetCombatZone(combatZone);
                }
            }
        }
        KCDC::Common::Replication::PoseSnapshot pose;
        bool poseHeld = false;
        if (!_snapshots.Sample(KCDC::Common::Replication::PoseRenderTime(_snapshots), pose, &poseHeld)) {
            // Nothing buffered yet: the wire pose is all there is, and for an
            // NPC that is a real answer rather than a placeholder -- the server
            // placed it there.
            pose.position     = position;
            pose.velocity     = velocity;
            pose.rotation     = rotation;
            pose.moveSpeedTag = moveSpeedTag;
            pose.moveDirTag   = moveDirTag;
            pose.stanceTag    = stanceTag;
            pose.inAir        = inAir;
        }
        if (poseHeld) {
            KCDC::Common::Replication::HoldLocomotion(pose);
        }
        if (!KCDC::Common::IsFinite(pose.position)) {
            return;
        }

        // An observer reproduces and decides nothing. `ReportPose` reads the
        // pace, direction and stance the simulating engine settled on straight
        // off that body, so an observer states them and places the reported
        // pose. Steering here instead gives two peers the same intent and lets
        // them disagree about arrival.
        KCDC::Common::PlaceEntity(entity, pose.position, pose.rotation);
        PublishLocomotion(pose);

        // The head, from the direction the simulator published. Cheap and worth
        // it: a guard that looks at what it is walking towards reads as alive.
        auto *movement = actor->GetMovementController();
        if (movement == nullptr) {
            return;
        }
        SDK::CMovementRequest request {};
        if (!headLookReady) {
            if (_headLookApplied) {
                request.ClearLookTarget();
                movement->RequestMovement(request);
                _headLookApplied = false;
            }
            return;
        }

        constexpr float kLookDistance = 10.0F;
        const glm::vec3 eye           = pose.position + glm::vec3(0.0F, 0.0F, 1.6F);
        const glm::vec3 target        = eye + glm::normalize(headLookDirection) * kLookDistance;
        request.SetLookTarget(KCDC::Common::ToNative(target));
        movement->RequestMovement(request);
        _headLookApplied = true;
    }

    // --- Simulator -----------------------------------------------------------

    void Npc::Simulate(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity) {
        // Reset the movement timer and status for a new order. A new revision
        // that still wants the body moving -- the next corner of a server
        // route, or a new destination -- keeps the steering request on the
        // body, so it walks on through the bend rather than stopping for the
        // frame it takes to be handed the next target.
        if (_runningRevision != intentRevision) {
            StopCombat(actor);
            _runningRevision = intentRevision;
            _reportedStatus  = Shared::Game::NpcIntentStatus::Idle;
            _movementProgress.Reset();
            glm::vec3 next {0.0F};
            if (!ResolveIntentTarget(next)) {
                ReleaseSteering(actor);
            }
            _snapshots.Clear();
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        const float dt           = _lastStep != 0 && now > _lastStep ? std::min(static_cast<float>(now - _lastStep) / 1000.0F, kMaxStepSeconds) : 0.0F;
        _lastStep                = now;

        // A scripted move of a body this client is simulating arrives as a warp,
        // because this peer is the only one that can put it anywhere.
        if (std::exchange(_forcedPosePending, false) && KCDC::Common::IsFinite(position)) {
            ReleaseSteering(actor);
            KCDC::Common::PlaceEntity(entity, position, rotation);
            velocity      = glm::vec3(0.0F);
            _reportedOnce = false;
            _lastReportAt = 0;
            _movementProgress.Reset();
        }

        ReportBusy(actor);

        // Frozen means frozen, whatever the intent says: a statue is a statue
        // even while it is being told to walk.
        if (Shared::Game::HasFlag(flags, Shared::Game::NpcFlags::Frozen)) {
            _movementProgress.Reset();
            ReleaseSteering(actor);

            // Nor does a blow move it. The hit reaction a blow queues
            // (`C_ActorActionHit`, from `ProcessActorHitRecord` 0x180B24600)
            // carries the body with its animation, and with no brain nothing
            // walks it back. NoMovementOverride is what every observer copy
            // already runs under (`ApplyOverride`): both movement controls
            // resolve to the entity, outranked only by a cutscene or the debug
            // slot (0x18083B7BC), the requested movement is cleared
            // (0x18083A604) and the merged movement is identity (0x18083A016),
            // so the reaction plays on the spot and the body neither travels
            // nor turns by animation (0x1808A3A2B).
            if (auto *animated = actor->GetAnimatedCharacterRuntime()) {
                animated->SetNoMovementOverride(true);
                _heldInPlace = true;
            }
            ReportPose(entity);
            return;
        }

        // Handed back on the edge only, once the server unfreezes the body:
        // the engine states the same override itself while a body is grabbed
        // (`SetInGrabbedState`, 0x181AAECE0).
        if (std::exchange(_heldInPlace, false)) {
            if (auto *animated = actor->GetAnimatedCharacterRuntime()) {
                animated->SetNoMovementOverride(false);
            }
        }

        // An animal's flee is the game's own escape, and its own brain runs it.
        if (FleesNatively()) {
            Escape(actor, entity);
            return;
        }

        glm::vec3 target {0.0F};
        const bool wantsMotion = ResolveIntentTarget(target);
        if (intent.kind == Shared::Game::NpcIntentKind::Attack && (!wantsMotion || TargetActor() == nullptr)) {
            StopCombat(actor);
            ReleaseSteering(actor);
            // Construction and delegation arrive independently. Wait for the
            // target body; the server cancels a target that no longer exists.
            ReportStatus(Shared::Game::NpcIntentStatus::Running);
            ReportPose(entity);
            return;
        }

        if (!wantsMotion) {
            _movementProgress.Reset();
            ReleaseSteering(actor);

            // A look is the one still intent that does something.
            if (intent.kind == Shared::Game::NpcIntentKind::LookAt || intent.kind == Shared::Game::NpcIntentKind::Talk) {
                glm::vec3 lookAt = intent.point;
                if (intent.targetNetworkId != 0) {
                    if (auto *other = ResolveSibling(intent.targetNetworkId)) {
                        lookAt = other->position;
                    }
                }
                if (KCDC::Common::IsFinite(lookAt)) {
                    FaceTowards(lookAt, dt);
                    const glm::vec3 eye = position + glm::vec3(0.0F, 0.0F, 1.6F);
                    headLookDirection   = glm::normalize(glm::vec3(lookAt.x, lookAt.y, lookAt.z + 1.6F) - eye);
                    headLookReady       = KCDC::Common::IsFinite(headLookDirection);
                }
            }
            else {
                headLookReady = false;
            }

            // Standing still is a state, not a failure: it is reported once so
            // a script waiting on a `lookAt` hears about it.
            if (_reportedStatus != Shared::Game::NpcIntentStatus::Reached) {
                ReportStatus(Shared::Game::NpcIntentStatus::Reached);
            }
            _publishedSpeedTag = kNoTag;
            if (locomotion == Shared::Game::NpcLocomotion::Kinematic) {
                // Holds the pose and animates the idle. In native mode the
                // engine owns the body, and posing it here would fight it.
                StepKinematic(actor, entity, position, dt);
            }
            else {
                ReportPose(entity);
            }
            return;
        }

        if (!KCDC::Common::IsFinite(target)) {
            ReportStatus(Shared::Game::NpcIntentStatus::Failed);
            ReleaseSteering(actor);
            return;
        }

        // Arrival, measured on the ground plane: a waypoint on a slope is still
        // arrived at, and an NPC standing on a step is not two metres away --
        // though one standing under a balcony has not reached a point on it.
        // An engaged fight is given slack past its radius, so a step back
        // inside an exchange does not break off into a chase.
        const bool attack       = intent.kind == Shared::Game::NpcIntentKind::Attack;
        const float engaged     = intent.bow ? Shared::Game::NpcIntent::kBowEngagedRange : IsAnimal() ? Shared::Game::NpcIntent::kMeleeAttackRadius + 0.5F : Shared::Game::NpcIntent::kMeleeEngagedRange;
        const bool keepFighting = attack && _combatTarget != 0 && glm::distance(position, target) <= engaged;
        const bool arrived      = attack && intent.bow ? glm::distance(position, target) <= intent.radius : intent.HasArrived(position, target);
        if (keepFighting || arrived) {
            if (attack) {
                ReleaseSteering(actor);
                _movementProgress.Reset();
                ReportStatus(Fight(actor) ? Shared::Game::NpcIntentStatus::Running : Shared::Game::NpcIntentStatus::Failed);
                ReportPose(entity);
                return;
            }
            _movementProgress.Reset();
            // A corner with route beyond it is walked through rather than
            // stopped at: the request already on the body keeps it moving for
            // the moment the server takes to hand over the next corner.
            if (intent.kind != Shared::Game::NpcIntentKind::MoveTo || intent.pathBeyond <= 0.0F) {
                ReleaseSteering(actor);
            }
            if (_reportedStatus != Shared::Game::NpcIntentStatus::Reached && _reportedStatus != Shared::Game::NpcIntentStatus::Blocked) {
                ReportStatus(Shared::Game::NpcIntentStatus::Reached);
            }
            _publishedSpeedTag = kNoTag;
            if (locomotion == Shared::Game::NpcLocomotion::Kinematic) {
                StepKinematic(actor, entity, position, dt);
            }
            else {
                ReportPose(entity);
            }
            return;
        }

        StopCombat(actor);

        // Following a moving player or taking a detour need not reduce the
        // distance to the target. Measure the body's headway instead. Blocked
        // is latched for the revision: the server acts on it once, and a body
        // that later shuffles free must not take it back and report it again.
        if (_reportedStatus != Shared::Game::NpcIntentStatus::Blocked) {
            const auto status = _movementProgress.Update(position, dt) ? Shared::Game::NpcIntentStatus::Blocked : Shared::Game::NpcIntentStatus::Running;
            if (_reportedStatus != status) {
                ReportStatus(status);
            }
        }

        if (locomotion == Shared::Game::NpcLocomotion::Native) {
            SteerNative(actor, target);
            // The engine owns the body in this mode, so the pose is read rather
            // than written -- and the tags with it, which is the whole reason
            // this mode exists.
            ReportPose(entity);
            return;
        }

        StepKinematic(actor, entity, target, dt);
    }

    bool Npc::IsAttacking() const {
        return alive && intent.kind == Shared::Game::NpcIntentKind::Attack && !Shared::Game::HasFlag(flags, Shared::Game::NpcFlags::Frozen);
    }

    bool Npc::HasAttackOrder() const {
        return IsSimulating() && IsAttacking();
    }

    bool Npc::FleesNatively() const {
        return IsSimulating() && alive && IsAnimal() && intent.kind == Shared::Game::NpcIntentKind::Flee && !Shared::Game::HasFlag(flags, Shared::Game::NpcFlags::Frozen);
    }

    MafiaNet::Time Npc::CombatRenderTime() {
        return KCDC::Common::Replication::PoseRenderTime(_snapshots);
    }

    SDK::wh::entitymodule::C_Actor *Npc::TargetActor() const {
        if (auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(intent.targetNetworkId)) {
            return human->ResolveBody();
        }
        if (auto *npc = KCDC::Common::FindReplicaById<Npc>(intent.targetNetworkId); npc != nullptr && npc != this && npc->alive) {
            return npc->ResolveActor();
        }
        return nullptr;
    }

    bool Npc::Fight(SDK::wh::entitymodule::C_Actor *actor) {
        auto *target = HasAttackOrder() ? TargetActor() : nullptr;
        if (target == nullptr) {
            return false;
        }
        if (intent.bow) {
            return Shoot(actor, target);
        }
        auto *fighter = actor->GetOrCreateCombatActor();
        if (fighter == nullptr || !fighter->IsInitialised()) {
            return false;
        }
        if (_combatTarget != target->GetEntityId()) {
            StopCombat(actor);
            _combatTarget    = target->GetEntityId();
            auto *automation = fighter->GetAutomation();
            automation->SetDefenseEnabled(true, _defenseToken);
            automation->SetGuardEnabled(true, _guardToken);
            fighter->SetTarget(target->GetEntity());
            if (auto *hands = actor->GetItemAttachmentManager()) {
                hands->DrawWeaponSet(SDK::wh::entitymodule::E_WeaponSet::Primary);
            }
            // The player-facing stance request selects freeGuard, which filters
            // out this NPC's opening attacks. Enter its native default mode.
            fighter->TryEnterCombatMode();
            Framework::Logging::GetLogger(kLogger)->info("NPC {} prepares native combat against {}", GetNetworkID(), intent.targetNetworkId);
            return true;
        }
        // Wait for entry to succeed before the attack selector starts. Retry
        // while drawing the weapon or another native restriction delays entry.
        if (!IsAnimal() && _offenseToken == 0 && fighter->TryEnterCombatMode()) {
            fighter->GetAutomation()->SetOffenseEnabled(true, _offenseToken);
        }
        if (IsAnimal() && MafiaNet::GetTime() >= _nextBiteAt && fighter->TryEnterCombatMode()) {
            // Wolves' automatic offense selects paired takedowns, whose two
            // bodies are not carried by the ordinary swing stream. Select a
            // native bite instead; its collision and damage use that stream.
            const auto outcome = fighter->TryUnpairedAttack(SDK::wh::combatmodule::C_CombatInputClassDatabase::GetAttackHeavyId());
            _nextBiteAt        = MafiaNet::GetTime() + (outcome == SDK::wh::combatmodule::C_CombatActor::AttackOutcome::Swung ? 2500 : 500);
        }
        return true;
    }

    bool Npc::Shoot(SDK::wh::entitymodule::C_Actor *actor, SDK::wh::entitymodule::C_Actor *target) {
        auto *actions = actor->GetActionActor();
        auto *hands   = actor->GetItemAttachmentManager();
        if (actions == nullptr || hands == nullptr) {
            return false;
        }
        const auto now = MafiaNet::GetTime();
        if (_combatTarget != target->GetEntityId()) {
            StopCombat(actor);
            _combatTarget = target->GetEntityId();
            _bowReadyBy   = now + Shared::Game::NpcIntent::kBowDrawHoldMs;
            hands->DrawWeaponSet(SDK::wh::entitymodule::E_WeaponSet::Secondary);
        }
        auto aim = target->GetEntity()->GetWorldPos();
        aim.z += 1.2F;
        headLookDirection = glm::normalize(glm::vec3(aim.x, aim.y, aim.z) - position - glm::vec3(0.0F, 0.0F, 1.6F));
        headLookReady     = KCDC::Common::IsFinite(headLookDirection);
        if (auto *movement = actor->GetMovementController()) {
            SDK::CMovementRequest request {};
            request.SetAimTarget(aim);
            request.SetFireTarget(aim);
            request.SetLookTarget(aim);
            request.SetBodyTarget(aim);
            request.SetDesiredSpeed(0.0F);
            movement->RequestMovement(request);
            _steering = true;
        }
        // The draw that brings the bow out takes a moment; one that never
        // arms it is a body that cannot carry out this order.
        auto *shooting = actions->GetShootingExpansion();
        if (shooting == nullptr || !shooting->IsActive()) {
            if (now < _bowReadyBy) {
                return true;
            }
            Framework::Logging::GetLogger(kLogger)->warn("NPC {} bow is not ready: expansion={} weapon={} ammo={}", GetNetworkID(), shooting != nullptr, shooting != nullptr && shooting->GetWeapon() != nullptr, shooting != nullptr && shooting->GetAmmunition() != nullptr);
            return false;
        }
        const auto weaponClass = shooting->GetWeapon()->InvokeGetItemClassRaw();
        const auto *weapon     = Shared::Game::FindMissileClass({weaponClass.m_low, weaponClass.m_high});
        if (weapon == nullptr || !weapon->weapon || !weapon->requiresCharging || weapon->requiresCocking) {
            return false;
        }
        const bool active   = shooting->HasShootingAction();
        const bool charging = active && shooting->IsCharging();
        const bool firing   = active && shooting->IsFiring();
        if (charging) {
            if (_bowReleaseAt == 0) {
                _bowReleaseAt = now + Shared::Game::NpcIntent::kBowDrawHoldMs;
            }
            if (now >= _bowReleaseAt && shooting->CanFire()) {
                shooting->Fire();
                _bowReleaseAt = 0;
            }
        }
        else if (!firing) {
            _bowReleaseAt = 0;
            // `attack` documents that an NPC's equipped arrows are replenished
            // while it shoots: two keep the next draw armed through the reload.
            if (auto *ammo = shooting->GetAmmunition(); ammo != nullptr && ammo->GetAmount() < 2) {
                ammo->AdjustAmount(2 - ammo->GetAmount());
            }
            if (shooting->CanCharge()) {
                shooting->Charge();
            }
            else if (!(active && shooting->IsReloading()) && shooting->CanReload()) {
                shooting->Reload(true, false);
            }
        }
        return true;
    }

    void Npc::StopCombat(SDK::wh::entitymodule::C_Actor *actor) {
        if (_combatTarget == 0) {
            return;
        }
        if (auto *actions = actor != nullptr ? actor->GetActionActor() : nullptr) {
            if (auto *shooting = actions->FindShootingExpansion(); shooting != nullptr && shooting->HasShootingAction()) {
                if (shooting->IsCharging()) {
                    shooting->CancelCharging();
                }
                if (shooting->IsReloading()) {
                    shooting->CancelReloading();
                }
            }
        }
        if (auto *movement = actor != nullptr ? actor->GetMovementController() : nullptr) {
            SDK::CMovementRequest request {};
            request.ClearAimTarget();
            request.ClearFireTarget();
            movement->RequestMovement(request);
        }
        // Standing down never builds a combat actor: one that was never built
        // holds no token, no target and no stance.
        if (auto *fighter = actor != nullptr ? actor->GetCombatActor() : nullptr; fighter != nullptr && fighter->IsInitialised()) {
            auto *automation = fighter->GetAutomation();
            if (_offenseToken != 0) {
                automation->SetOffenseEnabled(false, _offenseToken);
            }
            if (_defenseToken != 0) {
                automation->SetDefenseEnabled(false, _defenseToken);
            }
            if (_guardToken != 0) {
                automation->SetGuardEnabled(false, _guardToken);
            }
            fighter->SetTarget(nullptr);
            fighter->LeaveCombatModeIfActive();
            fighter->RequestFightingStance(false);
        }
        _offenseToken = 0;
        _defenseToken = 0;
        _guardToken   = 0;
        _combatTarget = 0;
        _nextBiteAt   = 0;
        _bowReleaseAt = 0;
        _bowReadyBy   = 0;
    }

    void Npc::Escape(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity) {
        // The brain moves the body from here on: nothing of this simulator's
        // stays on the movement controller, and observers are given no head
        // look of its choosing.
        ReleaseSteering(actor);
        headLookReady = false;
        ReportPose(entity);

        // Where the threat is: its replica while this client sees it, else
        // the point the order carries -- the place, or where the threat stood
        // when the order was given.
        auto *threat         = intent.targetNetworkId != 0 ? ResolveSibling(intent.targetNetworkId) : nullptr;
        const glm::vec3 from = threat != nullptr ? threat->position : intent.point;

        // A new order lets the brain think before anything is sent to it. The
        // escape's mailbox is not the brain's own: the animal switch tree's
        // `switch_animal_processInterruptMessages` registers it as its
        // mailbox group when it starts (the tree start, 0x180411BBC), and a
        // brain held since its body spawned has never started that tree. A
        // message no mailbox takes is dropped, so the escape is offered every
        // frame until the brain takes it.
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_escapeRevision != intentRevision && _reportedStatus != Shared::Game::NpcIntentStatus::Failed) {
            _escapeRevision = intentRevision;
            _escapeDueBy    = now + kEscapeHandOverMs;
        }

        if (_escapeDueBy != 0) {
            // The escape the game's own animals take: that mailbox runs
            // `callInterrupt_animal_escape` -- the species picks the pace and
            // the distance, and a flee move takes its own path. Given the
            // threat's WUID the move runs from the threat itself; without one
            // it runs from `from`. `ignorePriority` passes the priority check
            // another reaction already running would win.
            // Both WUIDs are the NPC records' own: a spawned body has no
            // EntityGUID to look one up by.
            auto *npcs       = SDK::wh::xgenaimodule::C_NPCManager::GetInstance();
            auto *self       = npcs->GetNPCByEntityId(_entityId);
            auto *threatBody = TargetActor();
            auto *threatNpc  = threatBody != nullptr ? npcs->GetNPCByEntityId(threatBody->GetEntityId()) : nullptr;
            std::string content;
            if (threatNpc != nullptr) {
                content = std::string(KCDC_HIDE("escapeFromWuid(")) + std::to_string(threatNpc->GetWuid().value);
            }
            else {
                content = std::string(KCDC_HIDE("useVec3ForEscape(true),escapeFromVec3(")) + std::to_string(from.x) + "," + std::to_string(from.y) + "," + std::to_string(from.z);
            }
            content += std::string(KCDC_HIDE("),ignorePriority(true)"));
            const SDK::ScopedCryString type(std::string(KCDC_HIDE("switch:animal_interruptData:escape")).c_str());
            const SDK::ScopedCryString text(content.c_str());
            if (self != nullptr && SDK::wh::xgenaimodule::SendMessageToWuid(self->GetWuid(), type, text)) {
                _escapeDueBy = 0;
            }
            else if (now >= _escapeDueBy) {
                Framework::Logging::GetLogger(kLogger)->error("NPC {}'s brain took no native flee", GetNetworkID());
                StopEscape();
                ReportStatus(Shared::Game::NpcIntentStatus::Failed);
                return;
            }
        }

        // Reached once the requested distance separates them. The escape
        // itself runs as far as the animal takes it.
        if (_escapeRevision == intentRevision && _escapeDueBy == 0 && _reportedStatus != Shared::Game::NpcIntentStatus::Reached) {
            const auto status = glm::distance(glm::vec2(position), glm::vec2(from)) >= intent.radius ? Shared::Game::NpcIntentStatus::Reached : Shared::Game::NpcIntentStatus::Running;
            if (_reportedStatus != status) {
                ReportStatus(status);
            }
        }
    }

    void Npc::StopEscape() {
        if (_escapeRevision == 0) {
            return;
        }
        _escapeRevision = 0;
        _escapeDueBy    = 0;
        // The movement system runs the escape's move without the brain, so a
        // brain held again mid-escape would leave the body running. A corpse
        // is the engine's death's to handle.
        auto *npc = alive ? SDK::wh::xgenaimodule::C_NPCManager::GetInstance()->GetNPCByEntityId(_entityId) : nullptr;
        if (npc != nullptr) {
            SDK::wh::C_GlobalEnvironment::GetInstance()->GetXGenAIModule()->GetNavigation()->StopNpcMovement(npc);
        }
    }

    bool Npc::ResolveIntentTarget(glm::vec3 &target) const {
        switch (intent.kind) {
        case Shared::Game::NpcIntentKind::MoveTo:
            target = intent.point;
            return true;

        case Shared::Game::NpcIntentKind::Attack:
        case Shared::Game::NpcIntentKind::Follow: {
            auto *other = intent.targetNetworkId != 0 ? ResolveSibling(intent.targetNetworkId) : nullptr;
            if (other == nullptr) {
                return false;
            }
            target = other->position;
            return true;
        }

        case Shared::Game::NpcIntentKind::Flee: {
            target = intent.FleeDestination(position);
            return true;
        }

        default: return false;
        }
    }

    void Npc::StepKinematic(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity, const glm::vec3 &target, float dt) {
        const glm::vec2 planar = glm::vec2(target) - glm::vec2(position);
        const float distance   = glm::length(planar);
        glm::vec3 moved        = position;

        if (dt > 0.0F && distance > 0.001F) {
            const float step      = Shared::Game::NpcSpeedMetresPerSecond(intent.speed) * dt;
            const glm::vec2 delta = planar * (std::min(step, distance) / distance);
            moved.x += delta.x;
            moved.y += delta.y;
            FaceTowards(target, dt);
        }

        // The ground, every step: the server integrated a straight line through
        // a world it cannot see, and only the client has the terrain under it.
        float ground = moved.z;
        if (GroundAt(moved, ground)) {
            moved.z = ground;
        }

        const glm::vec3 travelled = moved - position;
        velocity                  = dt > 0.0F ? travelled / dt : glm::vec3(0.0F);
        position                  = moved;
        transformReady            = true;
        poseEpoch                 = spawnEpoch;

        KCDC::Common::PlaceEntity(entity, position, rotation);

        // Animated through the same override every other replicated body uses.
        // The pace tag is the body's own: nothing here drives the MoveSpeed
        // group, so the fragment is whatever the body holds and the blend
        // coordinate comes from the velocity published with it. Real gaits are
        // what `NpcLocomotion::Native` is for.
        KCDC::Common::Replication::PoseSnapshot pose;
        pose.position     = position;
        pose.velocity     = velocity;
        pose.rotation     = rotation;
        pose.moveSpeedTag = moveSpeedTag;
        pose.moveDirTag   = moveDirTag;
        pose.stanceTag    = intent.stanceTag != kNoTag ? intent.stanceTag : stanceTag;

        ReportPose(entity);
        (void)actor;
    }

    void Npc::SteerNative(SDK::wh::entitymodule::C_Actor *actor, const glm::vec3 &target) {
        auto *movement = actor->GetMovementController();
        if (movement == nullptr) {
            return;
        }

        SDK::CMovementRequest request {};
        request.SetMoveTarget(KCDC::Common::ToNative(target));
        request.SetDesiredSpeed(Shared::Game::NpcSpeedMetresPerSecond(intent.speed));

        // How much path is left, which the engine otherwise takes to be none:
        // `C_ActorMovementController::Update` (0x1804B8E88) hands it to the
        // corner smoother and the movement transitions, and at zero they brake
        // for the target and plan a stop transition there -- a body walking a
        // server route would stand idle at every corner.
        request.SetDistanceToPathEnd(glm::distance(glm::vec2(position), glm::vec2(target)) + intent.pathBeyond);

        // The pace, which is a different thing from the speed and the only
        // source of the MoveSpeed animation tag: an `E_MovementSpeed` ordinal
        // that the movement controller floors at walk and hands to the actor's
        // AI animation state, where the tag pass reads it. Without it the body
        // travels at the desired speed above and plays no walk cycle.
        request.SetPseudoSpeed(Shared::Game::NpcSpeedPseudoOrdinal(intent.speed));
        if (intent.faceTarget) {
            request.SetBodyTarget(KCDC::Common::ToNative(target));
        }
        const glm::vec3 eye = position + glm::vec3(0.0F, 0.0F, 1.6F);
        request.SetLookTarget(KCDC::Common::ToNative(glm::vec3(target.x, target.y, target.z + 1.6F)));
        movement->RequestMovement(request);
        _steering = true;

        headLookDirection = glm::normalize(glm::vec3(target.x, target.y, target.z + 1.6F) - eye);
        headLookReady     = KCDC::Common::IsFinite(headLookDirection);
    }

    void Npc::PublishLocomotion(const KCDC::Common::Replication::PoseSnapshot &pose) {
        auto *actor = ResolveActor();
        if (!IsPresentable(actor)) {
            return;
        }

        // Moving or not is read off the replicated tag rather than measured from
        // a velocity: the simulator's engine left the MoveSpeed group empty when
        // it decided this body was standing, and that is the answer, not a
        // threshold this mod invents.
        const bool moving         = pose.moveSpeedTag != kNoTag;
        const std::uint32_t state = moving ? SDK::wh::entitymodule::CAnimActionAIMovement::STATE_MOVEMENT : SDK::wh::entitymodule::CAnimActionAIMovement::STATE_IDLE;

        // The simulator's whole tag state only once one has published it. The
        // replica holds zeros until then, and no body's state is empty: stated
        // over the body, they wipe the tags only an event sets -- an empty
        // hand's `noweapon` -- and an unarmed body has no guard to fight in.
        const bool tagsPublished = std::any_of(tagState, tagState + kTagStateBytes, [](std::uint8_t tag) {
            return tag != 0;
        });
        KCDC::Core::gApplication->GetNpcLocomotionService().PublishLocomotionOverride(_entityId, state, pose.moveSpeedTag, pose.moveDirTag, pose.stanceTag, pose.velocity, glm::normalize(pose.rotation), tagsPublished ? tagState : nullptr);
        _locomotionPublished = true;
    }

    void Npc::WithdrawLocomotion() {
        if (!_locomotionPublished || _entityId == 0) {
            return;
        }
        if (auto *actor = ResolveActor()) {
            if (auto *animated = actor->GetAnimatedCharacterRuntime()) {
                animated->SetNoMovementOverride(false);
            }
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetNpcLocomotionService().PublishLocomotionOverride(_entityId, 0, kNoTag, kNoTag, kNoTag, glm::vec3(0.0F), glm::identity<glm::quat>());
        }
        _locomotionPublished = false;
    }

    void Npc::ReleaseSteering(SDK::wh::entitymodule::C_Actor *actor) {
        if (!_steering || actor == nullptr) {
            _steering = false;
            return;
        }
        if (auto *movement = actor->GetMovementController()) {
            KCDC::Common::ClearSteering(movement);
        }
        _steering = false;
    }

    void Npc::FaceTowards(const glm::vec3 &target, float dt) {
        const glm::vec2 planar = glm::vec2(target) - glm::vec2(position);
        if (glm::length(planar) < 0.001F || dt <= 0.0F) {
            return;
        }

        // KCD2's bodies face +Y, and yaw is about Z.
        const float wantedYaw  = std::atan2(-planar.x, planar.y);
        const glm::quat wanted = glm::angleAxis(wantedYaw, glm::vec3(0.0F, 0.0F, 1.0F));

        // Turned at a rate rather than snapped: a body that changes facing
        // between two frames reads as a glitch, and the turn fragments are
        // chosen from how fast the facing is moving.
        // The step is an angle, so the fraction handed to slerp is that angle
        // over the one still to cover rather than a guess.
        const glm::quat current = glm::normalize(rotation);
        const float dot         = std::clamp(std::fabs(glm::dot(current, wanted)), -1.0F, 1.0F);
        const float remaining   = 2.0F * std::acos(dot);
        const float maxStep     = kTurnRateRadPerSecond * dt;
        if (remaining <= maxStep || remaining <= 0.0001F) {
            rotation = wanted;
            return;
        }
        rotation = glm::normalize(glm::slerp(current, wanted, maxStep / remaining));
    }

    void Npc::ReportPose(SDK::IEntity *entity) {
        auto *actor         = ResolveActor();
        auto *fighter       = actor != nullptr ? actor->GetCombatActor() : nullptr;
        auto *model         = fighter != nullptr && fighter->IsInitialised() ? fighter->GetModel() : nullptr;
        combatStance        = model != nullptr && fighter->IsFightingStanceUp();
        combatGuard         = model != nullptr ? model->GetGuardRequest().GetEffective() : 0;
        combatZone          = model != nullptr ? model->GetCombatZone() : -1;
        combatStateRevision = intentRevision;

        if (entity == nullptr) {
            return;
        }

        // Read back off the body rather than trusted from what was asked for:
        // in native mode the engine decided where the body went, and in
        // kinematic mode the ground query may have moved it.
        const SDK::Vec3 worldPos = entity->GetWorldPos();
        const glm::vec3 where    = KCDC::Common::ToGlm(worldPos);
        if (!KCDC::Common::IsFinite(where)) {
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_reportedOnce && _lastReportAt != 0 && now > _lastReportAt) {
            // Velocity across the bracket between two reports rather than per
            // frame: this is what receivers extrapolate along, and a per-frame
            // derivative at a high frame rate is mostly noise.
            const float span = static_cast<float>(now - _lastReportAt) / 1000.0F;
            if (span > 0.0F) {
                velocity = (where - _lastReported) / span;
            }
        }
        _lastReported  = where;
        _lastReportAt  = now;
        _reportedOnce  = true;
        position       = where;
        rotation       = KCDC::Common::ToGlm(entity->GetWorldRotation());
        transformReady = true;
        poseEpoch      = spawnEpoch;

        // The tags the engine itself chose, which in native mode is the whole
        // point: a real walk, jog or run picked by the movement controller.
        if (auto *actor = ResolveActor()) {
            if (auto *tagState = actor->GetTagState()) {
                const auto pack = [](std::uint32_t tag) -> std::uint8_t {
                    return tag == SDK::CTagDefinition::kInvalidId || tag > 0xFEU ? kNoTag : static_cast<std::uint8_t>(tag);
                };
                moveSpeedTag = pack(tagState->GetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_SPEED)));
                moveDirTag   = pack(tagState->GetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_DIR)));
                stanceTag    = pack(tagState->GetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_STANCE)));

                // And the state whole, for every group the three above do not
                // name.
                static_assert(Shared::Entities::NpcEntity::kTagStateBytes == KCDC::Features::Npc::LocomotionOverride::kTagStateBytes, "The NPC's wire tag state and the locomotion override's must be the same width");
                const std::uint8_t *raw = tagState->GetRawState();
                for (std::size_t i = 0; i < Shared::Entities::NpcEntity::kTagStateBytes; ++i) {
                    this->tagState[i] = raw[i];
                }
            }
        }
    }

    void Npc::ReportStatus(Shared::Game::NpcIntentStatus status) {
        _reportedStatus = status;
        intentStatus    = status;
        // Against the revision being run, so the server can tell an answer to
        // this order from an answer to the one before it.
        statusRevision = _runningRevision;
    }

    void Npc::ReportBusy(SDK::wh::entitymodule::C_Actor *actor) {
        // What the engine says, rather than anything remembered: a shared
        // animation is an action on the body's own queue, and the engine is what
        // puts it there and takes it out again. This is the whole input to the
        // framework's handover deadline -- a body that says it is busy forever
        // is handed over anyway once the grace runs out.
        auto *actions   = actor->GetActionActor();
        const bool inAnimation = actions != nullptr && actions->IsInSharedAnimation();
        busy                   = inAnimation;
    }

    bool Npc::GroundAt(const glm::vec3 &point, float &outZ) {
        auto *physics = SDK::CPhysicalWorld::GetInstance();
        if (physics == nullptr) {
            return false;
        }
        SDK::Vec3 ground {};
        if (!physics->ResolveGround(KCDC::Common::ToNative(point), kGroundProbeUp, kGroundProbeDown, ground)) {
            return false;
        }
        outZ = ground.z;
        return true;
    }

    // --- One-shots -----------------------------------------------------------

    void Npc::PerformAction(const Shared::RPC::NpcAction &action) {
        auto *actor = ResolveActor();
        if (actor == nullptr || !_bodyReady) {
            return;
        }

        switch (action.kind) {
        case Shared::RPC::NpcAction::Emote:
            // Fire and forget: nothing reports its start or its end, because
            // nothing is waiting for either.
            Social::EmoteService::QueueEmoteOnActor(actor, action.animationId);
            break;

        case Shared::RPC::NpcAction::Speak: {
            auto *speak = actor->GetSpeakMan();
            if (speak == nullptr || action.text.empty()) {
                break;
            }
            // Speak copies these through the engine allocator, which reads the
            // refcount header behind each pointer, so they have to be
            // engine-owned and alive across the call.
            const SDK::ScopedCryString sound("");
            const SDK::ScopedCryString line(action.text.c_str());
            const SDK::ScopedCryString mood("");

            SDK::wh::entitymodule::S_SpeakData data {};
            data.soundEvent = sound.Get();
            data.text       = line.Get();
            data.mood       = mood.Get();
            data.unk0019    = 1;
            // Raw text rather than a key, and nothing about an NPC is player
            // controlled.
            data.isLocalizable      = false;
            data.isPlayerControlled = false;
            // The first gate asks whether the body is in a dialogue.
            data.force = true;
            // Long enough to read, floored so a short line does not blink out.
            data.duration    = std::max(3.0F, static_cast<float>(action.text.size()) * 0.06F);
            data.maxDistance = -1.0F;
            speak->Speak(data);
            break;
        }

        case Shared::RPC::NpcAction::Flinch:
            if (auto *ragdoll = actor->GetRagdollManager()) {
                // A nudge rather than a fall: the ragdoll blends back out on
                // its own, which is what a flinch looks like.
                ragdoll->RequestRagdoll(false);
            }
            break;

        default: break;
        }
    }
} // namespace KCDC::Features::Npc
