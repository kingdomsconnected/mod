#include <utils/safe_win32.h>

#include "features/player/player_missile_service.h"

#include "features/player/player_duel_service.h"

#include "common/module_support.h"
#include "core/application.h"
#include "core/obfuscation.h"
#include "features/ground_item/ground_item_service.h"
#include "features/npc/npc.h"
#include "features/npc/npc_service.h"
#include "features/player/player.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_pose.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/pe_params_particle.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_shooting_expansion.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_missile_weapon_runtime_data.h"
#include "sdk/wh/rpgmodule/c_event.h"
#include "sdk/wh/rpgmodule/c_rpg_module.h"
#include "shared/game/missile_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "features/player/player_inventory_service.h"
#include "features/player/player_rock_service.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_arrow.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_holder.h"
#include "sdk/wh/entitymodule/c_pickable_item.h"
#include "sdk/wh/rpgmodule/c_combat_soul.h"


namespace KCDC::Features::Player {
    thread_local PlayerMissileService::RemoteShot *PlayerMissileService::_replaying = nullptr;

    /** A shooter or target: a player's replica or an NPC's, read through the same questions. */
    struct PlayerMissileService::Body {
        Human *human                  = nullptr;
        KCDC::Features::Npc::Npc *npc = nullptr;
        Body()                        = default;
        Body(Human &value): human(&value) {}
        Body(KCDC::Features::Npc::Npc &value): npc(&value) {}
        explicit operator bool() const {
            return human != nullptr || npc != nullptr;
        }
        SDK::wh::entitymodule::C_Actor *ResolveActor() const {
            if (human != nullptr) {
                return human->ResolveBody();
            }
            return npc != nullptr ? npc->ResolveActor() : nullptr;
        }
        SDK::EntityId GetEntityId() const {
            auto *actor = ResolveActor();
            return actor != nullptr ? actor->GetEntityId() : 0;
        }
        std::uint64_t GetNetworkID() const {
            return human != nullptr ? human->GetNetworkID() : npc != nullptr ? npc->GetNetworkID() : 0;
        }
        std::uint32_t Incarnation() const {
            return human != nullptr ? human->soul.incarnation : npc != nullptr ? npc->spawnEpoch : 0;
        }
        std::uint32_t Revision() const {
            return human != nullptr ? human->soul.revision : npc != nullptr ? npc->intentRevision : 0;
        }
        std::uint8_t Epoch() const {
            return human != nullptr ? human->stateEpoch : npc != nullptr ? npc->stateEpoch : 0;
        }
        bool IsOwner() const {
            return human != nullptr ? human->IsOwner() : npc != nullptr && npc->IsSimulating();
        }
        bool HasAppliedSoulState() const {
            if (human != nullptr) {
                return human->IsOwner() ? human->HasCapturedLocalSoul() : human->HasAppliedSoulState();
            }
            return npc != nullptr && npc->alive && ResolveActor() != nullptr;
        }
        bool CanAct() const {
            if (human != nullptr) {
                return human->IsOwner() ? human->soul.CanAct() : human->AppliedSoulState().CanAct();
            }
            return npc != nullptr && npc->alive;
        }
        const Shared::RPC::MissileState &State() const {
            return human != nullptr ? human->missile : npc->missile;
        }
        static Body Find(std::uint64_t id) {
            if (auto *human = KCDC::Common::FindReplicaById<Human>(id)) {
                return *human;
            }
            if (auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(id)) {
                return *npc;
            }
            return {};
        }
        /** A remote player's body, or an NPC's this client only observes. */
        static Body FromSoul(const SDK::wh::rpgmodule::I_Soul *soul) {
            if (auto *human = Human::FindRemoteSoul(soul)) {
                return *human;
            }
            const auto npc = KCDC::Features::Npc::NpcService::FindCombatBodyOfSoul(soul);
            return npc && !npc->simulating ? Find(npc->networkId) : Body {};
        }
        static Body FromActor(SDK::EntityId id) {
            if (id == 0) {
                return {};
            }
            if (const auto npc = KCDC::Features::Npc::NpcService::FindCombatBody(id)) {
                return Find(npc->networkId);
            }
            Body result;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Human>([&](Human *human) {
                    Body body(*human);
                    if (body.GetEntityId() == id) {
                        result = body;
                    }
                });
            }
            return result;
        }
    };

    namespace {

        Shared::RPC::ItemClassId ClassOf(SDK::wh::entitymodule::C_Item *item) {
            if (item == nullptr) {
                return {};
            }

            const auto value = item->InvokeGetItemClassRaw();

            return {value.m_low, value.m_high};
        }

        SDK::wh::entitymodule::C_Actor *LocalActor() {
            auto *framework = SDK::IGameFramework::GetInstance();

            return static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
        }

        SDK::wh::entitymodule::C_Inventory *Inventory(SDK::wh::entitymodule::C_Actor *actor) {
            auto *runtime = actor != nullptr ? actor->GetInventorySoulRuntime() : nullptr;

            return runtime != nullptr && runtime->IsInitialisedFor(actor->GetSoul()) ? runtime->GetInventory() : nullptr;
        }

        SDK::wh::entitymodule::C_MissileWeaponRuntimeData *WeaponData(SDK::wh::entitymodule::C_Item *item) {
            const auto *entry = Shared::Game::FindMissileClass(ClassOf(item));

            return entry != nullptr && entry->weapon ? static_cast<SDK::wh::entitymodule::C_MissileWeaponRuntimeData *>(item->GetRuntimeData()) : nullptr;
        }

        bool IsValidState(const Shared::RPC::MissileState &state) {
            KCDC_JUNK;
            if (!state.IsSane() || !KCDC_OPAQUE_TRUE) {
                return KCDC_OBFI(false);
            }

            if (!state.weapon.IsValid()) {
                return KCDC_OBFI(true);
            }

            const auto *entry = Shared::Game::FindMissileClass(state.weapon);

            const bool valid = entry != nullptr && entry->weapon && (!state.ammunition.IsValid() || Shared::Game::IsCompatibleMissileAmmunition(state.weapon, state.ammunition))
                               && (!state.loadedAmmunition.IsValid() || Shared::Game::IsCompatibleMissileAmmunition(state.weapon, state.loadedAmmunition));
            return KCDC_OBFI(valid);
        }

        // Under a struck body's head, a mounted one's included.
        constexpr float kRecoveryProbeUp   = 0.5F;
        constexpr float kRecoveryProbeDown = 4.0F;

        SDK::IPhysicalEntity *CharacterPhysics(SDK::ICharacterInstance *character) {
            SDK::ISkeletonPose *pose = character != nullptr ? character->GetISkeletonPose() : nullptr;
            return pose != nullptr ? pose->GetCharacterPhysics() : nullptr;
        }

        bool ApplyItemProperties(SDK::wh::entitymodule::C_Item *item, const Shared::RPC::MissileItemProperties &properties) {
            if (properties.quality > item->InvokeGetMaxQualityRaw()) {
                return false;
            }

            item->SetQuality(properties.quality);
            item->InvokeSetItemHealthRaw(properties.health);

            auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(item->EnsureRuntimeData());

            if (runtime == nullptr) {
                return false;
            }

            runtime->SetPoison({properties.poisonBuff.low, properties.poisonBuff.high}, properties.poisonCharges);

            return item->InvokeGetQualityRaw() == properties.quality && std::abs(item->GetHealth() - properties.health) <= 0.0001F;
        }

    } // namespace

    SDK::wh::entitymodule::C_Item *PlayerMissileService::EnsureItem(SDK::wh::entitymodule::C_Inventory *inventory, const Shared::RPC::ItemClassId &itemClass, RemoteWeapon &state, int amount) {
        if (inventory == nullptr || !itemClass.IsValid()) {
            return nullptr;
        }

        const SDK::wh::entitymodule::S_ItemClassGUID guid {itemClass.low, itemClass.high};
        auto *item = inventory->InvokeFindItemByClassRaw(guid);

        if (item == nullptr) {
            auto &attempts = state.createAttempts[{itemClass.low, itemClass.high}];

            if (attempts >= 3) {
                return nullptr;
            }

            ++attempts;
            inventory->CreateItems(guid, amount, false);
            item = inventory->InvokeFindItemByClassRaw(guid);

            if (item == nullptr && attempts == 3) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile shadow item creation refused: class={:016X}{:016X}", itemClass.high, itemClass.low);
            }
        }

        return item;
    }

    void PlayerMissileService::ReleaseShotItems(ShotItems &items) {
        auto *actors = SDK::IActorSystem::GetInstance();
        auto *actor  = actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(items.actorId)) : nullptr;
        if (auto *inventory = Inventory(actor)) {
            for (const auto wuid : {items.weapon, items.ammunition}) {
                if (wuid != 0) {
                    if (auto *item = inventory->FindItemById(wuid)) {
                        inventory->RemoveItem(item);
                    }
                }
            }
        }
        items = {};
    }

    bool PlayerMissileService::PrepareShotItems(ShotItems &items, const Shared::RPC::MissileLaunched &launch, const Body &human) {
        auto *actor     = human.ResolveActor();
        auto *inventory = Inventory(actor);

        if (actor == nullptr || !actor->IsSettled() || inventory == nullptr || !Shared::Game::IsValidMissileLaunchProperties(launch)) {
            return false;
        }

        if (items.actorId != 0 && items.actorId != human.GetEntityId()) {
            ReleaseShotItems(items);
        }

        items.actorId = human.GetEntityId();

        if (items.failed) {
            return false;
        }

        auto &state        = _weapons[human.GetNetworkID()];
        const auto prepare = [&](std::uint64_t &wuid, const Shared::RPC::ItemClassId &itemClass, const Shared::RPC::MissileItemProperties &properties, int amount) {
            if (wuid != 0) {
                return inventory->FindItemById(wuid) != nullptr;
            }

            // A local player's replay must not create a persistent seed stack
            // when the shot spent their last arrow or they changed equipment.
            const bool localPlayer = human.human != nullptr && human.IsOwner();
            const SDK::wh::entitymodule::S_ItemClassGUID guid {itemClass.low, itemClass.high};
            auto *source         = localPlayer ? inventory->InvokeFindItemByClassRaw(guid) : EnsureItem(inventory, itemClass, state, amount);
            bool temporarySource = false;
            if (source == nullptr && localPlayer) {
                inventory->CreateItems(guid, amount, false);
                source          = inventory->InvokeFindItemByClassRaw(guid);
                temporarySource = true;
            }

            if (source == nullptr) {
                return false;
            }

            if (properties.quality > source->InvokeGetMaxQualityRaw()) {
                if (temporarySource) {
                    inventory->RemoveItem(source);
                }
                items.failed = true;

                return false;
            }

            // Clone per shot so concurrent hits cannot change each other's items.
            auto *item = temporarySource ? source : inventory->CloneItem(source, 10, true);

            if (item == nullptr) {
                items.failed = true;

                return false;
            }

            wuid = item->GetId();
            item->AdjustAmount(amount - static_cast<std::int32_t>(item->GetAmount()));

            if (!ApplyItemProperties(item, properties)) {
                items.failed = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
                    ->warn("Missile hit item rejected: shooter={} sequence={} weapon={} quality={}/{} health={:.6f}/{:.6f}", launch.shooterNetworkId, launch.sequence, itemClass == launch.weapon, item->InvokeGetQualityRaw(), properties.quality, item->GetHealth(),
                        properties.health);

                return false;
            }

            return true;
        };

        return prepare(items.weapon, launch.weapon, launch.weaponProperties, 1) && prepare(items.ammunition, launch.ammunition, launch.ammunitionProperties, 2);
    }

    void PlayerMissileService::ApplyWeapon(const Body &human) {
        auto *actor = human.ResolveActor();

        if (actor == nullptr || !actor->IsSettled() || actor->GetActionActor() == nullptr || !IsValidState(human.State())) {
            return;
        }

        auto &remote = _weapons[human.GetNetworkID()];

        if (remote.actorId != human.GetEntityId()) {
            remote.createAttempts.clear();
            remote.actorId              = human.GetEntityId();
            remote.awaitingDrawReset    = false;
            remote.releasedBeforeLaunch = false;
            remote.aimApplied           = false;
        }

        if (remote.desired.phase != human.State().phase || remote.desired.weapon != human.State().weapon) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote state: shooter={} phase={}->{}", human.GetNetworkID(), static_cast<int>(remote.desired.phase), static_cast<int>(human.State().phase));
        }

        remote.desired = human.State();

        if (remote.fireExpiresAt <= MafiaNet::GetTime() || (remote.desired.weapon.IsValid() && remote.fireWeapon != remote.desired.weapon)) {
            remote.fireAmmunition = {};
        }

        // The controller aims the upper body at the aim target and turns the
        // body after it ("aimIdleTurn", C_ActorMovementController::Update
        // 0x1804B8E88); without one the drawn weapon points wherever the body
        // faces, not where its owner aims. A player's aim is its camera, the
        // look direction the pose carries.
        if (remote.desired.weapon.IsValid()) {
            const bool lookReady   = human.human != nullptr ? human.human->headLookReady : human.npc->headLookReady;
            const glm::vec3 look   = human.human != nullptr ? human.human->headLookDirection : human.npc->headLookDirection;
            const SDK::Vec3 origin = actor->GetEntity()->GetWorldPos();
            if (auto *movement = actor->GetMovementController(); movement != nullptr && lookReady) {
                const glm::vec3 aim = glm::vec3(origin.x, origin.y, origin.z + 1.6F) + look * 20.0F;
                SDK::CMovementRequest request {};
                request.SetAimTarget({aim.x, aim.y, aim.z});
                request.SetFireTarget({aim.x, aim.y, aim.z});
                movement->RequestMovement(request);
            }
        }
        else if (auto *movement = actor->GetMovementController(); movement != nullptr && remote.aimApplied) {
            SDK::CMovementRequest request {};
            request.ClearAimTarget();
            request.ClearFireTarget();
            movement->RequestMovement(request);
        }
        remote.aimApplied = remote.desired.weapon.IsValid();

        if (human.npc != nullptr && remote.desired.weapon.IsValid()) {
            if (auto *hands = actor->GetItemAttachmentManager(); hands != nullptr && hands->GetSelectedWeaponSet() != SDK::wh::entitymodule::E_WeaponSet::Secondary) {
                hands->DrawWeaponSet(SDK::wh::entitymodule::E_WeaponSet::Secondary);
            }
        }
        auto *expansion = actor->GetActionActor()->FindShootingExpansion();

        if (expansion == nullptr) {
            return;
        }

        if (!remote.desired.weapon.IsValid()) {
            if (expansion->HasShootingAction() && expansion->IsCharging()) {
                expansion->CancelCharging();
            }

            if (expansion->HasShootingAction() && expansion->IsReloading()) {
                expansion->CancelReloading();
            }

            return;
        }

        auto *weapon = expansion->GetWeapon();

        if (ClassOf(weapon) != remote.desired.weapon) {
            return;
        }

        auto *inventory = Inventory(actor);
        auto *runtime   = WeaponData(weapon);

        if (inventory == nullptr || runtime == nullptr) {
            return;
        }

        const bool fire     = remote.fireAmmunition.IsValid();
        const auto selected = fire ? remote.fireAmmunition : remote.desired.ammunition;
        const auto loaded   = fire ? remote.fireAmmunition : remote.desired.loadedAmmunition;
        auto *ammo          = EnsureItem(inventory, selected, remote, 2);
        auto *loadedAmmo    = loaded == selected ? ammo : EnsureItem(inventory, loaded, remote, 2);

        if ((selected.IsValid() && ammo == nullptr) || (loaded.IsValid() && loadedAmmo == nullptr)) {
            return;
        }

        if (expansion->GetAmmunition() != ammo) {
            expansion->SetAmmunition(ammo);
        }

        if (!expansion->IsActive()) {
            return;
        }

        const auto phase = fire ? Shared::RPC::MissilePhase::Firing : remote.desired.phase;

        if (remote.desired.phase == Shared::RPC::MissilePhase::Idle || remote.desired.phase == Shared::RPC::MissilePhase::Reloading) {
            remote.awaitingDrawReset    = false;
            remote.releasedBeforeLaunch = false;
        }

        // The firing action owns unloading at its release marker. A draw
        // already released waits for its owner to leave the shot; only a
        // further launch draws again.
        if ((expansion->HasShootingAction() && expansion->IsFiring()) || (remote.awaitingDrawReset && !fire)) {
            return;
        }

        // Nothing else can be queued on a running reload from here: its next
        // stage or draw is queued as it finishes, by ContinueRemoteReload.
        if (expansion->HasShootingAction() && expansion->IsReloading()) {
            if (phase == Shared::RPC::MissilePhase::Idle) {
                expansion->CancelReloading();
            }

            return;
        }

        // Released on the owner's Firing phase, not on its launch: a hand
        // cannon's firing action burns its fuse for seconds before the
        // launch marker, and the flash and smoke are in that animation.
        if (expansion->HasShootingAction() && expansion->IsCharging()) {
            // An owner whose last reload stage has loaded (and cocked) its
            // round is about to draw: the draw the copy queued as its own
            // last stage finished stays.
            const bool reloaded = remote.desired.loadedAmmunition.IsValid() && (remote.desired.cocked || !Shared::Game::FindMissileClass(remote.desired.weapon)->requiresCocking);

            if (phase == Shared::RPC::MissilePhase::Firing && expansion->CanFire()) {
                expansion->Fire();
                remote.fireAmmunition       = {};
                remote.awaitingDrawReset    = true;
                remote.releasedBeforeLaunch = !fire;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote release requested: shooter={} pendingShot={}", human.GetNetworkID(), fire);
            }
            else if (phase == Shared::RPC::MissilePhase::Idle || (phase == Shared::RPC::MissilePhase::Reloading && !reloaded)) {
                expansion->CancelCharging();
            }

            return;
        }

        if (phase == Shared::RPC::MissilePhase::Reloading) {
            if (expansion->CanReload()) {
                expansion->Reload(true, false);
            }

            return;
        }

        const auto loadedWuid = loadedAmmo != nullptr ? loadedAmmo->GetId() : UINT64_MAX;
        const bool changed    = runtime->GetLoadedAmmunitionWuid() != loadedWuid || runtime->IsCocked() != (fire || remote.desired.cocked);

        if (runtime->GetLoadedAmmunitionWuid() != loadedWuid) {
            if (runtime->HasLoadedAmmunition()) {
                runtime->UnloadAmmunition();
            }

            if (loadedAmmo != nullptr) {
                runtime->LoadAmmunition(loadedAmmo);
            }
        }

        runtime->SetCocked(fire || remote.desired.cocked);

        if (changed) {
            expansion->RefreshAnimationTags();
        }

        if (human.HasAppliedSoulState() && human.CanAct() && (phase == Shared::RPC::MissilePhase::Charging || phase == Shared::RPC::MissilePhase::Firing) && expansion->CanCharge()) {
            expansion->Charge();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote draw requested: shooter={} pendingShot={}", human.GetNetworkID(), fire);
        }
    }

    void PlayerMissileService::DestroyRemote(RemoteShot &shot) {
        auto *projectile = shot.projectile;

        shot.projectile = nullptr;

        if (projectile != nullptr) {
            projectile->Destroy();
        }
    }

    void PlayerMissileService::LaunchRemote(RemoteShot &shot, const Body &human) {
        auto *actor     = human.ResolveActor();
        auto *inventory = Inventory(actor);

        if (shot.message.incarnation != human.Incarnation() || shot.message.epoch != human.Epoch() || actor == nullptr || !actor->IsSettled() || inventory == nullptr || shot.attempts >= 3) {
            return;
        }

        auto &state  = _weapons[human.GetNetworkID()];
        auto *weapon = EnsureItem(inventory, shot.message.weapon, state, 1);
        auto *ammo   = EnsureItem(inventory, shot.message.ammunition, state, 2);

        if (weapon == nullptr || ammo == nullptr) {
            return;
        }

        ++shot.attempts;

        SDK::Vec3 position {shot.message.position.x, shot.message.position.y, shot.message.position.z};
        const SDK::Vec3 direction {shot.message.direction.x, shot.message.direction.y, shot.message.direction.z};
        const SDK::wh::entitymodule::ProjectileOriginCorrectionLayout correction {};
        auto *previous = _replaying;

        _replaying = &shot;
        SDK::wh::entitymodule::C_ShootingSystem185484990::GetInstance()->SpawnAndLaunchProjectile(actor, 0, direction, correction, position, weapon, ammo, glm::length(shot.message.velocity), false);
        _replaying = previous;

        if (shot.projectile != nullptr) {
            shot.launched = true;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile observer replayed: shooter={} sequence={} speed={:.2f}", shot.message.shooterNetworkId, shot.message.sequence, glm::length(shot.message.velocity));
        }
    }

    bool PlayerMissileService::ApplyImpact(PendingImpact &pending, const Body &shooter) {
        const auto &impact = pending.message;
        auto *source       = shooter.ResolveActor();
        const auto victim  = Body::Find(impact.targetNetworkId);
        auto *target       = victim ? victim.ResolveActor() : nullptr;
        auto *inventory    = Inventory(source);
        auto *globals      = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *rpg          = globals != nullptr ? globals->GetRPGModule() : nullptr;

        if (source == nullptr || !source->IsSettled() || source->GetSoul() == nullptr || target == nullptr || target->GetSoul() == nullptr || inventory == nullptr || rpg == nullptr || rpg->GetEventManager() == nullptr) {
            return false;
        }

        if (!shooter.HasAppliedSoulState()) {
            return false;
        }

        // An NPC's state epoch moves with every override the server pushes to
        // its simulator -- an order, a health write, and the round this very
        // shot lodged in its stock (`Missile::OnRetained`), which reaches this
        // machine right behind the impact. Only its life fences the hit; the
        // referee checks the report against the epoch it then has.
        const bool targetMatches = victim && victim.IsOwner() && impact.soulAction.targetIncarnation == victim.Incarnation() && (victim.npc != nullptr || impact.soulAction.targetEpoch == victim.Epoch());
        if (!targetMatches || impact.soulAction.incarnation != shooter.Incarnation() || impact.soulAction.epoch != shooter.Epoch()) {
            return true;
        }

        if (!PrepareShotItems(pending.items, impact.launch, shooter)) {
            return false;
        }

        auto *weapon = inventory->FindItemById(pending.items.weapon);
        auto *ammo   = inventory->FindItemById(pending.items.ammunition);

        SDK::wh::rpgmodule::S_MissileHitData data;

        data.attacker.SetSoul(source->GetSoul());
        data.target.SetSoul(target->GetSoul());
        data.weaponWuid         = weapon->GetId();
        data.ammunitionWuid     = ammo->GetId();
        data.position           = {impact.position.x, impact.position.y, impact.position.z};
        data.launchPosition     = {impact.launch.position.x, impact.launch.position.y, impact.launch.position.z};
        data.velocity           = {impact.velocity.x, impact.velocity.y, impact.velocity.z};
        data.normal             = {impact.normal.x, impact.normal.y, impact.normal.z};
        data.bodyMaterial       = impact.bodyMaterial;
        data.surface            = impact.surface;
        data.ammunitionCategory = impact.ammunitionCategory;
        data.scale              = impact.scale;

        if (auto *attachments = target->GetItemAttachmentManager()) {
            data.defenseItemWuid = attachments->FindMissileDefenseItem().value;
        }

        SDK::wh::rpgmodule::S_EventHandle182D291D0 result;
        const float shooterStamina = source->GetSoul()->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina);
        const float victimStamina  = target->GetSoul()->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina);

        bool queriesGood = false;
        auto action      = impact.soulAction;
        {
            Human::SoulActionScope queries(action, source->GetSoul(), target->GetSoul(), weapon, ammo, Inventory(target) != nullptr ? Inventory(target)->FindItemById(data.defenseItemWuid) : nullptr, true);
            source->GetSoul()->GetCombatSoul()->ProcessMissileHit(&result, data);
            queriesGood = queries.Good();
        }
        if (!queriesGood) {
            if (result.event != nullptr) {
                result.event->Release();
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile impact discarded: missing attacker soul query, shooter={} sequence={}", impact.launch.shooterNetworkId, impact.launch.sequence);
            return true;
        }

        if (result.event != nullptr) {
            auto *sourceEffects   = SDK::wh::rpgmodule::C_Event::CreateForMissileHit(*rpg->GetEventManager(), data);
            bool accepted         = false;
            std::size_t discarded = 0;

            if (sourceEffects != nullptr) {
                discarded = static_cast<SDK::wh::rpgmodule::C_Event *>(result.event)->MoveEffectsTo(*sourceEffects, {data.attacker.soulWuid, data.weaponWuid, data.ammunitionWuid});
                sourceEffects->Release();

                // The health the arrow takes is the combat referee's to rule
                // on, as a blade's is: withheld here and reported.
                const PlayerDuelService::MissileHitScope blow(KCDC::Core::gApplication->GetPlayerDuelService(), target->GetSoul(), impact.launch.shooterNetworkId, impact.launch.sequence);
                accepted = rpg->GetEventManager()->ProcessEvent(result.event);
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile victim event partition allocation failed: shooter={} sequence={}", impact.launch.shooterNetworkId, impact.launch.sequence);
            }

            result.event->Release();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile hit processed: shooter={} sequence={} bodyMaterial={} nativeResult={} discardedSourceEffects={}", impact.launch.shooterNetworkId, impact.launch.sequence, impact.bodyMaterial, accepted, discarded);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)
                ->debug("Missile hit stamina: shooter={} sequence={} source={:.2f} revision={} victim={:.2f}->{:.2f}", impact.launch.shooterNetworkId, impact.launch.sequence, shooterStamina, impact.soulAction.revision, victimStamina,
                    target->GetSoul()->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina));
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile hit produced no native event: shooter={} sequence={}", impact.launch.shooterNetworkId, impact.launch.sequence);
        }

        // A false native result can include partial effects; do not retry.
        return true;
    }

    std::optional<SDK::Vec3> PlayerMissileService::GroundUnderStruckBody(SDK::wh::entitymodule::CProjectile *projectile, const SDK::Vec3 &position) const {
        // A round that struck a body and did not lodge -- a head, a helmet --
        // stays where it struck, in the air. It is laid on the ground under
        // that point instead, probed past the body's own two colliders, the
        // entity's and the animated skeleton's, either of which would stop
        // the ray.
        const auto shot = _local.find(projectile);
        const auto body = shot != _local.end() && shot->second.impactTarget != 0 ? Body::Find(shot->second.impactTarget) : Body {};
        auto *actor     = body ? body.ResolveActor() : nullptr;
        auto *physics   = SDK::CPhysicalWorld::GetInstance();
        if (actor == nullptr || physics == nullptr) {
            return std::nullopt;
        }
        SDK::IEntity *entity          = actor->GetEntity();
        SDK::IPhysicalEntity *skip[2] = {entity->GetPhysics(), CharacterPhysics(entity->GetCharacter(0))};
        SDK::Vec3 ground;
        const bool found = physics->ResolveGround(position, kRecoveryProbeUp, kRecoveryProbeDown, ground, skip, 2);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile round came off body {}: struck at z={:.2f}, ground {} z={:.2f}", shot->second.impactTarget, position.z, found ? "found" : "not found", ground.z);
        return found ? std::optional<SDK::Vec3> {ground} : std::nullopt;
    }

    void PlayerMissileService::RemoveRemoteAmmunition() {
        for (const auto &removal : _remoteAmmoRemovals) {
            const auto human = Body::Find(removal.targetNetworkId);

            if (!human || human.GetEntityId() != removal.actorId) {
                continue;
            }

            if (auto *inventory = Inventory(human.ResolveActor())) {
                if (auto *item = inventory->FindItemById(removal.itemWuid)) {
                    inventory->RemoveItem(item, 0x26, 1);
                }
            }
        }

        _remoteAmmoRemovals.clear();
    }
    bool PlayerMissileService::ProcessSourceEffects(SDK::wh::rpgmodule::I_EventManager *manager, SDK::wh::rpgmodule::I_Event *event, const SDK::wh::rpgmodule::S_MissileHitData &data) {
        auto *source = SDK::wh::rpgmodule::C_Event::CreateForMissileHit(*manager, data);

        if (source == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile source event partition allocation failed");

            return false;
        }

        const auto moved    = static_cast<SDK::wh::rpgmodule::C_Event *>(event)->MoveEffectsTo(*source, {data.attacker.soulWuid, data.weaponWuid, data.ammunitionWuid});
        const bool accepted = moved == 0 || manager->ProcessEvent(source);

        source->Release();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile source effects processed: count={} nativeResult={}", moved, accepted);

        return accepted;
    }

    bool PlayerMissileService::IsLocalActor(SDK::EntityId entityId) {
        auto *actor = LocalActor();

        return entityId != 0 && actor != nullptr && actor->GetEntityId() == entityId;
    }

    bool PlayerMissileService::IsSimulatedNpc(SDK::EntityId entityId) {
        const auto body = KCDC::Features::Npc::NpcService::FindCombatBody(entityId);
        return body && body->shooting;
    }

    bool PlayerMissileService::IsRemoteActor(const SDK::wh::entitymodule::C_Actor *actor) {
        const auto body = actor != nullptr ? Body::FromActor(actor->GetEntityId()) : Body {};
        return body && !body.IsOwner();
    }

    bool PlayerMissileService::IsReplayingLaunch() {
        return _replaying != nullptr;
    }

    bool PlayerMissileService::IsRemoteSoul(const SDK::wh::rpgmodule::I_Soul *soul) {
        return static_cast<bool>(Body::FromSoul(soul));
    }

    bool PlayerMissileService::IsReproducingDraw(const SDK::wh::rpgmodule::I_Soul *soul) {
        const auto body = Body::FromSoul(soul);
        if (!body || !body.CanAct()) {
            return false;
        }
        const auto weapon      = _weapons.find(body.GetNetworkID());
        const bool pendingShot = weapon != _weapons.end() && weapon->second.fireAmmunition.IsValid() && weapon->second.fireExpiresAt > MafiaNet::GetTime();
        return body.State().phase == Shared::RPC::MissilePhase::Charging || body.State().phase == Shared::RPC::MissilePhase::Firing || pendingShot;
    }

    bool PlayerMissileService::TryGetRemoteReloadSpeed(const SDK::wh::rpgmodule::I_Soul *soul, float &speed) {
        const auto body = Body::FromSoul(soul);
        if (!body || !body.State().weapon.IsValid() || !IsValidState(body.State())) {
            return false;
        }
        speed = body.State().reloadSpeed;
        return true;
    }

    bool PlayerMissileService::IsObserverProjectile(const SDK::wh::entitymodule::CProjectile *projectile) const {
        return _observers.contains(projectile);
    }

    bool PlayerMissileService::IsNpcProjectile(SDK::wh::entitymodule::CProjectile *projectile) const {
        const auto shot = _local.find(projectile);
        return shot != _local.end() && shot->second.message.npcRevision != 0;
    }

    bool PlayerMissileService::IsTrackedLocalProjectile(SDK::wh::entitymodule::CProjectile *projectile) const {
        return _local.contains(projectile);
    }

    void PlayerMissileService::BindReplayedProjectile(SDK::wh::entitymodule::CProjectile *projectile) {
        if (_replaying != nullptr) {
            _replaying->projectile = projectile;
            _observers.insert(projectile);

            // Pierceability >15 skips particle contacts while preserving flight.
            auto *physics = projectile->GetPhysicalEntity();

            if (physics != nullptr && physics->GetType() == SDK::PE_PARTICLE) {
                SDK::pe_params_particle params;

                params.iPierceability = SDK::pe_params_particle::kNoCollisionPierceability;

                if (physics->SetParams(&params, 0) == 0) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile observer collision filter rejected: sequence={}", _replaying->message.sequence);
                }
            }
        }
        else {
            // Pool reuse starts a new lifetime without necessarily destroying it.
            _observers.erase(projectile);
        }
    }

    bool PlayerMissileService::TrackLocalLaunch(const Shared::RPC::MissileLaunched &message, SDK::wh::entitymodule::CProjectile *projectile) {
        if (projectile == nullptr || _local.size() >= Shared::RPC::MissileLaunched::kMaxActivePerPlayer || _local.contains(projectile)) {
            return false;
        }

        _local.emplace(projectile, LocalShot {message.sequence, MafiaNet::GetTime() + Shared::RPC::MissileLaunched::kLifetimeMs, message});
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile launched: sequence={} speed={:.2f}", message.sequence, glm::length(message.velocity));

        return true;
    }

    bool PlayerMissileService::CaptureImpact(SDK::wh::entitymodule::CProjectile *projectile, const SDK::wh::rpgmodule::S_MissileHitData &data, Shared::RPC::MissileImpact &report) {
        const auto shot   = _local.find(projectile);
        auto *replication = Framework::CoreModules::GetReplication();

        if (shot == _local.end() || replication == nullptr) {
            return false;
        }

        const auto target = Body::FromActor(data.target.entityId);
        if (target && (target.npc != nullptr || !target.IsOwner() || shot->second.message.npcRevision != 0)) {
            report.targetNetworkId = target.GetNetworkID();
        }

        report.launch             = shot->second.message;
        report.position           = {data.position.x, data.position.y, data.position.z};
        report.velocity           = {data.velocity.x, data.velocity.y, data.velocity.z};
        report.normal             = {data.normal.x, data.normal.y, data.normal.z};
        report.bodyMaterial       = data.bodyMaterial;
        report.surface            = data.surface;
        report.ammunitionCategory = data.ammunitionCategory;
        report.scale              = data.scale;

        if (report.targetNetworkId == 0 || (shot->second.impactTarget != 0 && shot->second.impactTarget != report.targetNetworkId)) {
            return false;
        }

        shot->second.impactTarget = report.targetNetworkId;

        return true;
    }

    bool PlayerMissileService::CaptureRetained(SDK::wh::entitymodule::CProjectile *projectile, SDK::wh::entitymodule::C_ItemHolder *destination, SDK::wh::entitymodule::C_Item *item, Shared::RPC::MissileRetained &report) {
        const auto shot = _local.find(projectile);

        if (shot == _local.end() || shot->second.impactTarget == 0 || item == nullptr || item->GetOwner() != destination || ClassOf(item) != shot->second.message.ammunition) {
            return false;
        }

        // Any body the impact was reported on: another player's, or an NPC's.
        const auto target = Body::Find(shot->second.impactTarget);

        SDK::wh::entitymodule::C_Inventory *inventory = target && (target.npc != nullptr || !target.IsOwner()) ? Inventory(target.ResolveActor()) : nullptr;
        if (inventory == nullptr || inventory != destination) {
            return false;
        }

        SDK::wh::entitymodule::MissileLaunch::ItemProperties properties;

        properties.Capture(item);
        report.shooterNetworkId = shot->second.message.shooterNetworkId;
        report.sequence        = shot->second.sequence;
        report.targetNetworkId = shot->second.impactTarget;
        report.properties      = {properties.quality, properties.health, {properties.poisonBuff.hipart, properties.poisonBuff.lopart}, properties.poisonCharges};

        if (!report.IsSane() || !Shared::Game::IsValidRetainedMissileProperties(report.properties, shot->second.message.ammunitionProperties)) {
            return false;
        }

        return true;
    }

    void PlayerMissileService::QueueRetainedAmmunitionRemoval(std::uint64_t targetNetworkId, SDK::wh::entitymodule::C_Item *item) {
        const auto target = Body::Find(targetNetworkId);

        if (target) {
            _remoteAmmoRemovals.push_back({targetNetworkId, target.GetEntityId(), item->GetId()});
        }
    }

    std::uint64_t PlayerMissileService::ForgetProjectile(SDK::wh::entitymodule::CProjectile *projectile, bool destructed) {
        if (destructed) {
            _observers.erase(projectile);
        }

        for (auto &[key, shot] : _remote) {
            if (shot.projectile == projectile) {
                shot.projectile = nullptr;
            }
        }

        const auto local = _local.find(projectile);

        if (local == _local.end()) {
            return 0;
        }

        const auto sequence = local->second.sequence;

        _local.erase(local);

        return sequence;
    }

    void PlayerMissileService::QueueRecoveredItem(SDK::wh::entitymodule::C_Item *item) {
        if (item != nullptr && _recoveredItems.size() < Shared::RPC::MissileLaunched::kMaxActivePerPlayer) {
            _recoveredItems.push_back(item->GetId());
        }
    }

    Shared::RPC::MissileState PlayerMissileService::CaptureLocalState() {
        return CaptureState(LocalActor());
    }

    Shared::RPC::MissileState PlayerMissileService::CaptureState(SDK::wh::entitymodule::C_Actor *actor) {
        Shared::RPC::MissileState state;
        auto *actions   = actor != nullptr ? actor->GetActionActor() : nullptr;
        auto *expansion = actions != nullptr ? actions->FindShootingExpansion() : nullptr;

        if (expansion == nullptr) {
            return state;
        }

        auto *weapon = expansion->GetWeapon();
        auto *data   = WeaponData(weapon);

        if (data == nullptr) {
            return state;
        }

        auto *ammo      = expansion->GetAmmunition();
        auto *inventory = Inventory(actor);

        state.weapon           = ClassOf(weapon);
        state.ammunition       = ClassOf(ammo);
        state.ammunitionAmount = ammo != nullptr ? ammo->GetAmount() : 0;
        state.cocked           = data->IsCocked();

        if (auto *soul = actor->GetSoul()) {
            state.reloadSpeed = soul->ApplyModifiers(SDK::wh::rpgmodule::I_Soul::kReloadAnimationSpeedModifier, 1.0F, nullptr);
        }

        if (inventory != nullptr && data->HasLoadedAmmunition()) {
            state.loadedAmmunition = ClassOf(inventory->FindItemById(data->GetLoadedAmmunitionWuid()));
        }

        // Asked first because this runs on engine edges now, and two of them
        // are `C_ActorActionShootingBase::Exit`, which clears the sub-action
        // the three predicates below dereference. No sub-action, no phase.
        if (expansion->HasShootingAction()) {
            state.phase = expansion->IsFiring() ? Shared::RPC::MissilePhase::Firing : expansion->IsReloading() ? Shared::RPC::MissilePhase::Reloading : expansion->IsCharging() ? Shared::RPC::MissilePhase::Charging : Shared::RPC::MissilePhase::Idle;
        }

        return state;
    }

    // --- The local missile state ---------------------------------------------

    void PlayerMissileService::SeedLocalState(Human &owner) {
        _localState   = CaptureLocalState();
        owner.missile = _localState;
    }

    void PlayerMissileService::NotifyLocalStateChanged() {
        const auto state = CaptureLocalState();

        if (state == _localState) {
            return;
        }

        auto *owner = KCDC::Features::Player::Human::FindLocal();

        // Before the body exists there is nothing to write to, and nothing to
        // remember either: leaving _localState alone keeps the first real
        // publish from being suppressed as a no-op.
        if (owner == nullptr) {
            return;
        }

        if (_localState.phase != state.phase || _localState.weapon != state.weapon) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile local state: phase={}->{} reloadSpeed={:.3f}", static_cast<int>(_localState.phase), static_cast<int>(state.phase), state.reloadSpeed);
        }

        _localState    = state;
        owner->missile = state;
    }

    void PlayerMissileService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (net == nullptr) {
            return;
        }

        const auto queue = [](const auto &message) {
            if (!message.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }

            auto &service = KCDC::Core::gApplication->GetPlayerMissileService();
            std::lock_guard lock(service._incomingMutex);

            if (service._acceptIncoming && service._incoming.size() < 512) {
                service._incoming.emplace_back(message);
            }
        };

        net->RegisterRPC<Shared::RPC::MissileLaunched>([queue](const Shared::RPC::MissileLaunched &message, MafiaNet::Packet *) {
            queue(message);
        });
        net->RegisterRPC<Shared::RPC::MissileEnded>([queue](const Shared::RPC::MissileEnded &message, MafiaNet::Packet *) {
            queue(message);
        });
        net->RegisterRPC<Shared::RPC::MissileImpact>([queue](const Shared::RPC::MissileImpact &message, MafiaNet::Packet *) {
            queue(message);
        });
    }

    void PlayerMissileService::RemoteWeaponFired(SDK::wh::entitymodule::C_Actor *actor) {
        auto *actions   = actor != nullptr ? actor->GetActionActor() : nullptr;
        auto *expansion = actions != nullptr ? actions->FindShootingExpansion() : nullptr;

        if (expansion != nullptr && expansion->GetWeapon() != nullptr) {
            expansion->MarkWeaponFired();
        }

        auto *replication = Framework::CoreModules::GetReplication();

        if (replication != nullptr) {
            replication->ForEach<Human>([&](Human *human) {
                if (human->IsOwner() || human->GetEntityId() != actor->GetEntityId()) {
                    return;
                }

                const auto weapon   = _weapons.find(human->GetNetworkID());
                const auto sequence = weapon != _weapons.end() ? weapon->second.fireSequence : 0;

                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote animation released: shooter={} sequence={} activeShot={}", human->GetNetworkID(), sequence, _remote.contains({human->GetNetworkID(), sequence}));
            });
        }
    }

    void PlayerMissileService::ContinueRemoteReload(SDK::wh::entitymodule::C_Actor *actor) {
        const auto body = Body::FromActor(actor->GetEntityId());
        auto *expansion = actor->GetActionActor()->FindShootingExpansion();

        // Only a finishing reload stage: after a shot the copy's round is
        // still loaded (its FireWeapon is skipped), which would pass a draw.
        if (!body || !expansion->HasShootingAction() || !expansion->IsReloading() || !IsValidState(body.State()) || body.State().phase == Shared::RPC::MissilePhase::Idle) {
            return;
        }

        // The native controller's order (RefreshMissileActionMap 0x182AA9E0C).
        if (expansion->CanReload()) {
            expansion->Reload(true, false);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote reload continued: shooter={}", body.GetNetworkID());
        }
        else if (body.HasAppliedSoulState() && body.CanAct() && expansion->CanCharge()) {
            expansion->Charge();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile remote draw requested: shooter={} fromReload=true", body.GetNetworkID());
        }
    }

    void PlayerMissileService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (replication == nullptr) {
            return;
        }

        RemoveRemoteAmmunition();

        std::vector<std::variant<Shared::RPC::MissileLaunched, Shared::RPC::MissileEnded, Shared::RPC::MissileImpact>> incoming;

        {
            std::lock_guard lock(_incomingMutex);
            _acceptIncoming = true;
            incoming.swap(_incoming);
        }

        const auto now = MafiaNet::GetTime();

        for (const auto &event : incoming) {
            if (const auto *launch = std::get_if<Shared::RPC::MissileLaunched>(&event)) {
                const auto human = Body::Find(launch->shooterNetworkId);

                if (!human || human.IsOwner() || launch->incarnation != human.Incarnation() || launch->epoch != human.Epoch() || !Shared::Game::IsCompatibleMissileAmmunition(launch->weapon, launch->ammunition) || !Shared::Game::IsValidMissileLaunchProperties(*launch)) {
                    continue;
                }

                auto &weapon = _weapons[launch->shooterNetworkId];

                if (launch->sequence <= weapon.lastSequence) {
                    continue;
                }

                weapon.lastSequence = launch->sequence;
                weapon.fireSequence = launch->sequence;

                if (weapon.releasedBeforeLaunch) {
                    weapon.releasedBeforeLaunch = false;
                }
                else {
                    weapon.fireAmmunition = launch->ammunition;
                    weapon.fireWeapon     = launch->weapon;
                    weapon.fireExpiresAt  = now + 1000;
                }

                const auto active = std::count_if(_remote.begin(), _remote.end(), [&](const auto &entry) {
                    return entry.first.first == launch->shooterNetworkId;
                });

                if (active < Shared::RPC::MissileLaunched::kMaxActivePerPlayer) {
                    _remote.emplace(std::pair<std::uint64_t, std::uint64_t> {launch->shooterNetworkId, launch->sequence}, RemoteShot {*launch, nullptr, now + Shared::RPC::MissileLaunched::kLifetimeMs});
                }
            }
            else if (const auto *end = std::get_if<Shared::RPC::MissileEnded>(&event)) {
                const auto found = _remote.find({end->shooterNetworkId, end->sequence});

                if (found != _remote.end()) {
                    DestroyRemote(found->second);
                    _remote.erase(found);
                }
            }
            else if (const auto *hit = std::get_if<Shared::RPC::MissileImpact>(&event)) {
                const auto target = Body::Find(hit->targetNetworkId);
                const auto source = Body::Find(hit->launch.shooterNetworkId);

                if (target && target.IsOwner() && source && (target.npc != nullptr || source.npc != nullptr || !source.IsOwner()) && Shared::Game::IsHumanMissileBodyMaterial(hit->bodyMaterial)
                    && Shared::Game::IsCompatibleMissileAmmunition(hit->launch.weapon, hit->launch.ammunition) && Shared::Game::IsValidMissileLaunchProperties(hit->launch) && _impacts.size() < 256) {
                    _impacts.try_emplace({hit->launch.shooterNetworkId, hit->launch.sequence}, PendingImpact {*hit, now + Shared::RPC::MissileLaunched::kLifetimeMs});
                }
            }
        }

        // Queue release first; close-range impact can precede its callback.
        replication->ForEach<Human>([this](Human *human) {
            if (!human->IsOwner()) {
                ApplyWeapon(*human);
            }
        });

        replication->ForEach<KCDC::Features::Npc::Npc>([this](auto *npc) {
            if (npc->IsSimulating()) {
                npc->missile = CaptureState(npc->ResolveActor());
            }
            else {
                ApplyWeapon(Body(*npc));
            }
        });

        for (auto it = _remote.begin(); it != _remote.end();) {
            const auto human = Body::Find(it->first.first);

            if (!human || it->second.expiresAt <= now) {
                DestroyRemote(it->second);
                it = _remote.erase(it);
                continue;
            }

            if (!it->second.launched && !it->second.abandoned) {
                if (it->second.expiresAt - now > Shared::RPC::MissileLaunched::kLifetimeMs - 1000) {
                    LaunchRemote(it->second, human);
                }
                else {
                    it->second.abandoned = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Missile observer replay expired: shooter={} sequence={}", it->first.first, it->first.second);
                }
            }

            ++it;
        }

        for (auto it = _local.begin(); it != _local.end();) {
            if (it->second.expiresAt <= now) {
                if (KCDC::Core::gApplication != nullptr) {
                    NotifyEnded(it->second.sequence);
                }

                it = _local.erase(it);
            }
            else {
                ++it;
            }
        }

        for (auto it = _impacts.begin(); it != _impacts.end();) {
            const auto shooter = Body::Find(it->first.first);

            if (!shooter || it->second.expiresAt <= now) {
                ReleaseShotItems(it->second.items);
                it = _impacts.erase(it);
                continue;
            }

            if (!it->second.finished) {
                if (it->second.expiresAt - now > Shared::RPC::MissileLaunched::kLifetimeMs - 1000) {
                    std::optional<PlayerInventoryService::AuthorityProjection> projection;
                    const bool localPlayer = shooter.human != nullptr && shooter.IsOwner();
                    if (localPlayer) {
                        projection.emplace(KCDC::Core::gApplication->GetPlayerInventoryService());
                    }
                    it->second.finished = ApplyImpact(it->second, shooter);
                    if (localPlayer) {
                        ReleaseShotItems(it->second.items);
                    }
                }
                else {
                    it->second.finished = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile victim replay expired before actor/items/stamina were ready: shooter={} sequence={}", it->first.first, it->first.second);
                }
            }

            ++it;
        }

        if (!_recoveredItems.empty()) {
            auto *world = KCDC::Features::GroundItem::WorldInventory();

            if (world != nullptr) {
                for (const auto wuid : _recoveredItems) {
                    if (auto *item = world->FindItemById(wuid)) {
                        KCDC::Core::gApplication->GetGroundItemService().OnLocalDrop(item, false);
                    }
                }
            }

            _recoveredItems.clear();
        }
    }

    void PlayerMissileService::ForgetActor(std::uint64_t networkId) {
        RemoveRemoteAmmunition();

        for (auto it = _remote.begin(); it != _remote.end();) {
            if (it->first.first == networkId) {
                DestroyRemote(it->second);
                it = _remote.erase(it);
            }
            else {
                ++it;
            }
        }

        _weapons.erase(networkId);

        for (auto it = _impacts.begin(); it != _impacts.end();) {
            if (it->first.first == networkId) {
                ReleaseShotItems(it->second.items);
                it = _impacts.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // --- Local engine edges ------------------------------------------------

    void PlayerMissileService::NotifyLaunched(SDK::EntityId actorEntityId, const SDK::wh::entitymodule::MissileLaunch &launch) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        bool connected    = false;

        if (replication != nullptr) {
            replication->ForEach<Human>([&](Human *human) {
                connected = connected || human->IsOwner();
            });
        }

        if (!connected || net == nullptr || (!IsLocalActor(actorEntityId) && !IsSimulatedNpc(actorEntityId)) || _nextMissileSequence == 0) {
            return;
        }

        Shared::RPC::MissileLaunched report;
        const auto body = Body::FromActor(actorEntityId);
        if (!body || !body.IsOwner()) {
            return;
        }
        report.incarnation      = body.Incarnation();
        report.epoch            = body.Epoch();
        report.shooterNetworkId = body.GetNetworkID();
        report.npcRevision      = body.npc != nullptr ? body.Revision() : 0;

        report.sequence             = body.npc != nullptr ? (static_cast<std::uint64_t>(body.Revision()) << 32U) | (_nextMissileSequence++ & 0xFFFFFFFFULL) : _nextMissileSequence++;
        report.weaponProperties     = {launch.weaponProperties.quality, launch.weaponProperties.health, {launch.weaponProperties.poisonBuff.hipart, launch.weaponProperties.poisonBuff.lopart}, launch.weaponProperties.poisonCharges};
        report.ammunitionProperties = {launch.ammunitionProperties.quality, launch.ammunitionProperties.health, {launch.ammunitionProperties.poisonBuff.hipart, launch.ammunitionProperties.poisonBuff.lopart}, launch.ammunitionProperties.poisonCharges};
        report.weapon               = {launch.weaponClass.m_low, launch.weaponClass.m_high};
        report.ammunition           = {launch.ammunitionClass.m_low, launch.ammunitionClass.m_high};
        report.position             = {launch.position.x, launch.position.y, launch.position.z};
        report.direction            = {launch.direction.x, launch.direction.y, launch.direction.z};
        report.velocity             = {launch.velocity.x, launch.velocity.y, launch.velocity.z};

        if (report.IsSane() && TrackLocalLaunch(report, launch.projectile)) {
            _shotOwners[report.sequence] = body.GetNetworkID();
            net->BroadcastRPC(report);
        }
    }

    void PlayerMissileService::NotifyEnded(std::uint64_t sequence) {
        _reportedMissileImpacts.erase(sequence);
        _reportedRetainedMissiles.erase(sequence);

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (net != nullptr) {
            Shared::RPC::MissileEnded report;

            report.sequence = sequence;
            if (const auto owner = _shotOwners.find(sequence); owner != _shotOwners.end()) {
                report.shooterNetworkId = owner->second;
                _shotOwners.erase(owner);
            }
            net->BroadcastRPC(report);
        }
    }

    void PlayerMissileService::NotifyAmmunitionRecovered(SDK::wh::entitymodule::C_Item *item) {
        QueueRecoveredItem(item);
    }

    void PlayerMissileService::NotifyImpactBegan(SDK::wh::entitymodule::CProjectile *projectile, const SDK::wh::rpgmodule::S_MissileHitData &data, MissileImpactCapture &capture) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *actors      = SDK::IActorSystem::GetInstance();
        auto *source      = static_cast<SDK::wh::entitymodule::C_Actor *>(actors != nullptr ? actors->GetActor(data.attacker.entityId) : nullptr);
        auto *target      = static_cast<SDK::wh::entitymodule::C_Actor *>(actors != nullptr ? actors->GetActor(data.target.entityId) : nullptr);
        auto *runtime     = source != nullptr ? source->GetInventorySoulRuntime() : nullptr;
        auto *inventory   = runtime != nullptr && runtime->IsInitialisedFor(source->GetSoul()) ? runtime->GetInventory() : nullptr;
        if (replication == nullptr || inventory == nullptr || target == nullptr || target->GetSoul() == nullptr || !CaptureImpact(projectile, data, capture.report)) {
            return;
        }
        // Only our own shot at someone else's copy is worth recording: a
        // hit we are merely watching is the attacker's to report.
        if (!IsLocalActor(data.attacker.entityId) && !IsSimulatedNpc(data.attacker.entityId)) {
            return;
        }

        auto &action = capture.report.soulAction;
        const auto shooter = Body::FromActor(data.attacker.entityId);
        const auto victim  = Body::FromActor(data.target.entityId);
        if (!shooter || !victim || !shooter.IsOwner() || !victim.HasAppliedSoulState()) {
            return;
        }
        action.incarnation = shooter.Incarnation();
        action.revision    = shooter.Revision();
        action.epoch       = shooter.Epoch();
        if (action.incarnation != capture.report.launch.incarnation || action.epoch != capture.report.launch.epoch) {
            return;
        }
        action.targetIncarnation = victim.Incarnation();
        action.targetEpoch       = victim.Epoch();
        auto *targetRuntime      = target->GetInventorySoulRuntime();
        auto *targetInventory    = targetRuntime != nullptr && targetRuntime->IsInitialisedFor(target->GetSoul()) ? targetRuntime->GetInventory() : nullptr;
        capture.queries.emplace(action, source->GetSoul(), target->GetSoul(), inventory->FindItemById(data.weaponWuid), inventory->FindItemById(data.ammunitionWuid), targetInventory != nullptr ? targetInventory->FindItemById(data.defenseItemWuid) : nullptr, false);
        capture.routed = true;
    }

    bool PlayerMissileService::NotifyImpactCompleted(MissileImpactCapture &capture) {
        if (!capture.routed) {
            return false;
        }

        // Close the capture scope before validation; its destructor restores
        // attacker resources.
        auto &action    = capture.report.soulAction;
        action.complete = capture.queries->Good();
        capture.queries.reset();

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return true;
        }
        if (!capture.report.IsSane()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Missile impact rejected: attacker soul queries could not be recorded, sequence={} complete={} queries={}", capture.report.launch.sequence, action.complete, action.count);
            return true;
        }
        if (_reportedMissileImpacts.insert(capture.report.launch.sequence).second) {
            net->BroadcastRPC(capture.report);
        }
        return true;
    }

    void PlayerMissileService::NotifyAmmunitionRetained(SDK::wh::entitymodule::CProjectile *projectile, SDK::wh::entitymodule::C_ItemHolder *destination, SDK::wh::entitymodule::C_Item *item) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        Shared::RPC::MissileRetained report;
        if (const auto shot = _local.find(projectile); shot != _local.end() && shot->second.message.npcRevision != 0) {
            if (item != nullptr && shot->second.impactTarget != 0) {
                QueueRetainedAmmunitionRemoval(shot->second.impactTarget, item);
            }
            return;
        }

        if (net != nullptr && CaptureRetained(projectile, destination, item, report) && _reportedRetainedMissiles.insert(report.sequence).second) {
            QueueRetainedAmmunitionRemoval(report.targetNetworkId, item);
            net->BroadcastRPC(report);
        }
    }

    void PlayerMissileService::Reset() {
        RemoveRemoteAmmunition();
        {
            std::lock_guard lock(_incomingMutex);
            _acceptIncoming = false;
            _incoming.clear();
        }

        _local.clear();
        _shotOwners.clear();

        for (auto &[key, shot] : _remote) {
            DestroyRemote(shot);
        }

        _remote.clear();

        for (auto &[key, impact] : _impacts) {
            ReleaseShotItems(impact.items);
        }

        _impacts.clear();
        _weapons.clear();
        _recoveredItems.clear();

        // Only ever tested for membership, never dereferenced, so dropping the
        // set cannot outlive anything: what it marks is gone with the replays
        // destroyed above.
        _observers.clear();

        // _replaying is not cleared here. It brackets one native call on one
        // thread and is restored by that call, so it is null outside one.

        _localState          = {};
        _nextMissileSequence = 1;
        _reportedMissileImpacts.clear();
        _reportedRetainedMissiles.clear();
    }
} // namespace KCDC::Features::Player

namespace SDK::wh::rpgmodule {
    class C_RPGCombat;
}

namespace KCDC::Features::Player {
    namespace {
        using Spawn_t        = void(__fastcall *)(SDK::wh::entitymodule::C_ShootingSystem185484990 *, SDK::wh::entitymodule::C_Actor *, std::int32_t, const SDK::Vec3 *, const SDK::wh::entitymodule::ProjectileOriginCorrectionLayout *, SDK::Vec3 *, SDK::wh::entitymodule::C_Item *,
            SDK::wh::entitymodule::C_Item *, float, bool);
        using Launch_t       = void(__fastcall *)(SDK::wh::entitymodule::CProjectile *, const SDK::Vec3 *, const SDK::Vec3 *, const SDK::Vec3 *, float);
        using Fire_t         = void(__fastcall *)(SDK::wh::entitymodule::C_ShootingSystem185484990 *, SDK::wh::entitymodule::C_Actor *, SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_Item *, SDK::EntityId, std::uint32_t, float);
        using TryDischarge_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionShootingMain *);
        using Destroy_t      = bool(__fastcall *)(SDK::wh::entitymodule::CProjectile *);
        using Destructor_t   = void(__fastcall *)(SDK::wh::entitymodule::CProjectile *);
        using Collision_t    = bool(__fastcall *)(SDK::wh::entitymodule::CArrow *, const SDK::EventPhysCollision *);
        using Recover_t      = SDK::wh::entitymodule::C_PickableItem *(__fastcall *)(SDK::wh::entitymodule::CArrow *, const SDK::Vec3 *, const SDK::Vec3 *);
        using Hit_t          = SDK::wh::rpgmodule::S_EventHandle182D291D0 *(__fastcall *)(SDK::wh::rpgmodule::C_CombatSoul *, SDK::wh::rpgmodule::S_EventHandle182D291D0 *, const SDK::wh::rpgmodule::S_MissileHitData *);
        using Event_t        = bool(__fastcall *)(SDK::wh::rpgmodule::I_EventManager *, SDK::wh::rpgmodule::I_Event *);
        using AddItem_t      = SDK::wh::entitymodule::C_Item *(__fastcall *)(SDK::wh::entitymodule::C_ItemHolder *, SDK::wh::entitymodule::C_Item *, std::uint32_t, bool);
        using ShootingStamina_t = bool(__fastcall *)(const SDK::wh::rpgmodule::C_RPGCombat *, SDK::wh::rpgmodule::I_Soul *);
        using ShootingEnter_t   = bool(__fastcall *)(void *, void *);
        using ShootingExit_t    = void(__fastcall *)(void *);
        using ReloadStep_t      = void(__fastcall *)(void *);
        using SetAmmunition_t   = void(__fastcall *)(SDK::wh::entitymodule::C_ActorShootingExpansion *, SDK::wh::entitymodule::C_Item *);
        using HandledItem_t     = void(__fastcall *)(SDK::wh::entitymodule::C_ActorShootingExpansion *, std::int32_t, const void *, const void *);

        Spawn_t s_spawn                     = nullptr;
        Launch_t s_launch                   = nullptr;
        Fire_t s_fire                       = nullptr;
        TryDischarge_t s_tryDischarge       = nullptr;
        Destroy_t s_destroy                 = nullptr;
        Destructor_t s_destructor           = nullptr;
        Collision_t s_collision             = nullptr;
        Recover_t s_recover                 = nullptr;
        Hit_t s_hit                         = nullptr;
        Event_t s_event                     = nullptr;
        AddItem_t s_addItem                 = nullptr;
        ShootingStamina_t s_shootingStamina = nullptr;
        ShootingEnter_t s_shootingEnter     = nullptr;
        ShootingExit_t s_shootingExit       = nullptr;
        ShootingExit_t s_shootingMainExit   = nullptr;
        ReloadStep_t s_cockWeapon           = nullptr;
        ReloadStep_t s_loadAmmunition       = nullptr;
        SetAmmunition_t s_setAmmunition     = nullptr;
        HandledItem_t s_handledItem         = nullptr;

        bool __fastcall ShootingEnter_Hook(void *action, void *previousActionHandle) {
            // The second argument is an opaque smart handle rather than an
            // action pointer, so it is passed straight through. A derived
            // Enter can refuse before reaching this one, and then no phase
            // was entered.
            const bool entered = s_shootingEnter(action, previousActionHandle);

            if (entered && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }

            return entered;
        }

        void __fastcall ShootingExit_Hook(void *action) {
            s_shootingExit(action);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        void __fastcall ShootingMainExit_Hook(void *action) {
            s_shootingMainExit(action);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        // The reload animation cocks the weapon and loads a round without
        // leaving the reloading sub-state, so neither shows up on an Enter or
        // an Exit.
        void __fastcall CockWeapon_Hook(void *action) {
            s_cockWeapon(action);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        void __fastcall LoadAmmunition_Hook(void *action) {
            s_loadAmmunition(action);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        void __fastcall SetAmmunition_Hook(SDK::wh::entitymodule::C_ActorShootingExpansion *expansion, SDK::wh::entitymodule::C_Item *item) {
            s_setAmmunition(expansion, item);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        // Bound by the expansion's constructor to the actor's handled-item
        // signal, and the only writer of the expansion's weapon: it clears it
        // when the tracked one leaves the slot and sets it when a usable
        // missile weapon arrives in it.
        void __fastcall HandledItemChanged_Hook(SDK::wh::entitymodule::C_ActorShootingExpansion *expansion, std::int32_t equipmentSlot, const void *previousItemId, const void *currentItemId) {
            s_handledItem(expansion, equipmentSlot, previousItemId, currentItemId);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLocalStateChanged();
            }
        }

        // Let an accepted draw finish if exhaustion arrives before its launch.
        bool __fastcall ShootingStamina_Hook(const SDK::wh::rpgmodule::C_RPGCombat *combat, SDK::wh::rpgmodule::I_Soul *soul) {
            return s_shootingStamina(combat, soul) || (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerMissileService().IsReproducingDraw(soul));
        }

        thread_local SDK::wh::entitymodule::MissileLaunch *s_capture             = nullptr;
        thread_local SDK::wh::entitymodule::CProjectile *s_colliding             = nullptr;
        thread_local SDK::wh::rpgmodule::I_Event *s_routedEvent                  = nullptr;
        thread_local const SDK::wh::rpgmodule::S_MissileHitData *s_routedHitData = nullptr;

        void __fastcall Spawn_Hook(SDK::wh::entitymodule::C_ShootingSystem185484990 *system, SDK::wh::entitymodule::C_Actor *actor, std::int32_t parameter, const SDK::Vec3 *direction, const SDK::wh::entitymodule::ProjectileOriginCorrectionLayout *correction, SDK::Vec3 *position,
            SDK::wh::entitymodule::C_Item *weapon, SDK::wh::entitymodule::C_Item *ammo, float speed, bool consume) {
            if (KCDC::Features::Player::PlayerMissileService::IsRemoteActor(actor) && !KCDC::Features::Player::PlayerMissileService::IsReplayingLaunch()) {
                return;
            }

            if (KCDC::Core::gApplication == nullptr || (!KCDC::Features::Player::PlayerMissileService::IsLocalActor(actor->GetEntityId()) && !KCDC::Features::Player::PlayerMissileService::IsSimulatedNpc(actor->GetEntityId()))) {
                s_spawn(system, actor, parameter, direction, correction, position, weapon, ammo, speed, consume);

                return;
            }

            // The launch spends the round natively; the spent unit is reported
            // next frame. Only projected ammunition can be spent.
            auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
            if (inventory.HasAuthority() && KCDC::Features::Player::PlayerMissileService::IsLocalActor(actor->GetEntityId())) {
                if (!inventory.CanSpend() || (consume && !inventory.ManagesNativeItem(ammo))) {
                    inventory.RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
                    return;
                }
                inventory.ArmConsumption(KCDC::Features::Player::PlayerInventoryService::ConsumptionKind::Shot);
            }

            SDK::wh::entitymodule::MissileLaunch capture;

            capture.weaponClass     = weapon->InvokeGetItemClassRaw();
            capture.ammunitionClass = ammo->InvokeGetItemClassRaw();
            capture.weaponProperties.Capture(weapon);
            capture.ammunitionProperties.Capture(ammo);
            capture.consumeAmmunition = consume;

            auto *previous = s_capture;

            s_capture = &capture;
            s_spawn(system, actor, parameter, direction, correction, position, weapon, ammo, speed, consume);
            s_capture = previous;

            if (capture.projectile != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyLaunched(actor->GetEntityId(), capture);
            }
        }

        void __fastcall Launch_Hook(SDK::wh::entitymodule::CProjectile *projectile, const SDK::Vec3 *position, const SDK::Vec3 *direction, const SDK::Vec3 *velocity, float scale) {
            // Register the observer before Launch can trigger collision callbacks.
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().BindReplayedProjectile(projectile);
            }
            s_launch(projectile, position, direction, velocity, scale);
            KCDC::Features::Player::PlayerRockService::Get().ProjectileLaunched(projectile);

            if (s_capture != nullptr) {
                s_capture->projectile = projectile;
                s_capture->position   = projectile->GetInitialPosition();
                s_capture->direction  = projectile->GetInitialDirection();
                s_capture->velocity   = projectile->GetInitialVelocity();
            }
        }

        void __fastcall Fire_Hook(SDK::wh::entitymodule::C_ShootingSystem185484990 *system, SDK::wh::entitymodule::C_Actor *actor, SDK::wh::entitymodule::C_Item *weapon, SDK::wh::entitymodule::C_Item *ammo, SDK::EntityId target, std::uint32_t mode, float power) {
            // Scatter ammunition is removed by FireWeapon after the pellet
            // launch calls, so the whole call is one armed spend.
            if (KCDC::Core::gApplication != nullptr && KCDC::Features::Player::PlayerMissileService::IsLocalActor(actor->GetEntityId())) {
                auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
                if (inventory.HasAuthority() && (!inventory.CanSpend() || (ammo != nullptr && !inventory.ManagesNativeItem(ammo)))) {
                    inventory.RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
                    return;
                }
                inventory.ArmConsumption(KCDC::Features::Player::PlayerInventoryService::ConsumptionKind::Shot);
            }
            // The launch RPC owns observer projectiles; this callback only animates.
            if (!KCDC::Features::Player::PlayerMissileService::IsRemoteActor(actor)) {
                s_fire(system, actor, weapon, ammo, target, mode, power);
            }
            else if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().RemoteWeaponFired(actor);
            }
        }

        bool __fastcall TryDischarge_Hook(SDK::wh::entitymodule::C_ActorActionShootingMain *action) {
            // This separate discharge path may remove a round without calling
            // FireWeapon. False is its existing no-discharge result.
            if (KCDC::Core::gApplication != nullptr) {
                auto *owner = action->GetActionActor();
                auto *actor = owner != nullptr ? owner->GetActor() : nullptr;
                if (actor != nullptr && KCDC::Features::Player::PlayerMissileService::IsLocalActor(actor->GetEntityId())) {
                    auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
                    if (inventory.HasAuthority() && !inventory.CanSpend()) {
                        inventory.RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
                        return false;
                    }
                    inventory.ArmConsumption(KCDC::Features::Player::PlayerInventoryService::ConsumptionKind::Shot);
                }
            }
            return s_tryDischarge(action);
        }

        void Forget(SDK::wh::entitymodule::CProjectile *projectile, bool destructed = false) {
            if (KCDC::Core::gApplication == nullptr) {
                return;
            }

            const auto sequence = KCDC::Core::gApplication->GetPlayerMissileService().ForgetProjectile(projectile, destructed);

            if (sequence != 0) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyEnded(sequence);
            }
        }

        bool __fastcall Destroy_Hook(SDK::wh::entitymodule::CProjectile *projectile) {
            Forget(projectile);

            return s_destroy(projectile);
        }

        void __fastcall Destructor_Hook(SDK::wh::entitymodule::CProjectile *projectile) {
            Forget(projectile, true);
            s_destructor(projectile);
        }

        bool __fastcall Collision_Hook(SDK::wh::entitymodule::CArrow *projectile, const SDK::EventPhysCollision *collision) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerMissileService().IsObserverProjectile(projectile)) {
                return false;
            }

            auto *previous        = s_colliding;
            auto *previousEvent   = s_routedEvent;
            auto *previousHitData = s_routedHitData;

            s_colliding     = projectile;
            s_routedEvent   = nullptr;
            s_routedHitData = nullptr;

            const bool result = s_collision(projectile, collision);

            s_colliding     = previous;
            s_routedEvent   = previousEvent;
            s_routedHitData = previousHitData;

            return result;
        }

        SDK::wh::rpgmodule::S_EventHandle182D291D0 *__fastcall Hit_Hook(SDK::wh::rpgmodule::C_CombatSoul *soul, SDK::wh::rpgmodule::S_EventHandle182D291D0 *result, const SDK::wh::rpgmodule::S_MissileHitData *data) {
            KCDC::Features::Player::PlayerMissileService::MissileImpactCapture capture;
            if (KCDC::Core::gApplication != nullptr && s_colliding != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyImpactBegan(s_colliding, *data, capture);
            }
            auto *returned = s_hit(soul, result, data);

            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerMissileService().NotifyImpactCompleted(capture)) {
                s_routedEvent   = result->event;
                s_routedHitData = data;
            }

            return returned;
        }

        bool __fastcall Event_Hook(SDK::wh::rpgmodule::I_EventManager *manager, SDK::wh::rpgmodule::I_Event *event) {
            if (event != nullptr && event == s_routedEvent) {
                return KCDC::Features::Player::PlayerMissileService::ProcessSourceEffects(manager, event, *s_routedHitData);
            }

            return s_event(manager, event);
        }

        SDK::wh::entitymodule::C_PickableItem *__fastcall Recover_Hook(SDK::wh::entitymodule::CArrow *projectile, const SDK::Vec3 *position, const SDK::Vec3 *direction) {
            auto *item       = projectile->GetAmmunitionItem();
            const bool local = KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerMissileService().IsTrackedLocalProjectile(projectile);

            // A delegated NPC's round is removed without granting inventory.
            // Recovery changes the item's WUID, so the world item is found
            // through the pickable it became.
            if (local && KCDC::Core::gApplication->GetPlayerMissileService().IsNpcProjectile(projectile)) {
                auto *made      = s_recover(projectile, position, direction);
                auto *world     = KCDC::Features::GroundItem::WorldInventory();
                auto *recovered = world != nullptr ? world->FindItemByPickable(made) : nullptr;
                if (recovered != nullptr) {
                    KCDC::Core::gApplication->GetPlayerMissileService().NotifyAmmunitionRecovered(recovered);
                }
                return made;
            }
            // Recovery transfers the item out of the projectile's wrapper and
            // changes its WUID. Resolve it through the returned pickable, then
            // let the server's ground replica adopt it. ProcessCollision still
            // needs the result to finish embedding it (0x182ACC62D).
            if (local && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
                const auto fired = item != nullptr && position != nullptr ? KCDC::Core::gApplication->GetPlayerMissileService().ClaimRecovery(projectile) : std::nullopt;
                const auto landed = item != nullptr ? item->InvokeGetItemClassRaw() : SDK::wh::entitymodule::S_ItemClassGUID {};
                // Probed before the round's pickable exists: its own collider
                // at the struck point would be the ground the probe finds.
                const auto ground = fired ? KCDC::Core::gApplication->GetPlayerMissileService().GroundUnderStruckBody(projectile, *position) : std::nullopt;
                auto *made        = s_recover(projectile, position, direction);
                auto *world       = KCDC::Features::GroundItem::WorldInventory();
                auto *recovered   = world != nullptr ? world->FindItemByPickable(made) : nullptr;
                if (fired && recovered != nullptr) {
                    const SDK::Vec3 &laid = ground ? *ground : *position;
                    KCDC::Core::gApplication->GetPlayerInventoryService().QueueAmmunitionRecovery(*fired, {landed.m_low, landed.m_high}, laid.x, laid.y, laid.z, recovered->GetId(), ground.has_value());
                }
                return made;
            }
            auto *pickable = s_recover(projectile, position, direction);

            if (pickable != nullptr && local && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyAmmunitionRecovered(item);
            }

            return pickable;
        }

        SDK::wh::entitymodule::C_Item *__fastcall AddItem_Hook(SDK::wh::entitymodule::C_ItemHolder *inventory, SDK::wh::entitymodule::C_Item *item, std::uint32_t count, bool flag) {
            // Null is not a refusal the callers survive: none of them has
            // ever seen one. The item goes in, and the next projection takes
            // out whatever the server does not hold.
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeInventoryChanged(inventory, item);
            }
            // Native merging can invalidate item; use the returned stack afterward.
            // A round lodged in a body goes where the game puts it, into that
            // body's inventory -- the server's, which MissileRetained asks it
            // to fill; this machine's copy keeps only the presentation.
            const bool retained = s_colliding != nullptr && item == s_colliding->GetAmmunitionItem() && item != nullptr && item->GetAmount() == 1 && count <= 1 && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerMissileService().IsTrackedLocalProjectile(s_colliding);
            auto *accepted      = s_addItem(inventory, item, count, flag);

            if (retained && accepted != nullptr && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerMissileService().NotifyAmmunitionRetained(s_colliding, inventory, accepted);
            }

            return accepted;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initMissileHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ShootingSystem__SpawnAndLaunchProjectile), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Spawn_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_spawn));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProjectile__Launch), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Launch_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_launch));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ShootingSystem__FireWeapon), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Fire_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_fire));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingMain__TryDischargeRaw), reinterpret_cast<LPVOID>(&KCDC::Features::Player::TryDischarge_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_tryDischarge));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGCombat__HasShootingStamina), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ShootingStamina_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_shootingStamina));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProjectile__Destroy), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Destroy_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_destroy));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProjectile__Destructor), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Destructor_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_destructor));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CArrow__ProcessCollision), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Collision_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_collision));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CArrow__SpawnRecoverableItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Recover_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_recover));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatSoul__ProcessMissileHit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Hit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_hit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_EventManager__ProcessEvent), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Event_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_event));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Inventory__AddItemRaw), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AddItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_addItem));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingBase__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ShootingEnter_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_shootingEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingBase__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ShootingExit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_shootingExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingMain__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ShootingMainExit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_shootingMainExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingReloading__CockWeapon), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CockWeapon_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_cockWeapon));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingReloading__LoadWeaponAmmunition), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LoadAmmunition_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_loadAmmunition));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorShootingExpansion__SetAmmunition), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetAmmunition_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_setAmmunition));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorShootingExpansion__OnHandledItemChanged), reinterpret_cast<LPVOID>(&KCDC::Features::Player::HandledItemChanged_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_handledItem));
    },
    "Missile");
