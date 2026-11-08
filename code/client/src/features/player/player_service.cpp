#include <utils/safe_win32.h>

#include "features/player/player_service.h"
#include "features/player/native_player_module.h"

#include "shared/features/player/player_appearance.h"

#include "features/player/player.h"
#include "sdk/wh/entitymodule/c_action_actor.h"

#include "features/cart/cart_service.h"
#include "features/horse/horse_service.h"
#include "features/player/player_missile_service.h"
#include "features/social/social_emote_service.h"
#include "core/presentation_module.h"

#include "features/player/player_nametags.h"
#include "features/player/player_teleport.h"
#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_procedural_context.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_equipment_handler.h"
#include "sdk/wh/entitymodule/c_actor_model.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_world_inventory.h"
#include "sdk/wh/entitymodule/e_actor_state.h"
#include "sdk/wh/entitymodule/i_actor_carry_corpse.h"
#include "sdk/wh/entitymodule/s_carry_item_action_params.h"

#include "common/module_support.h"

#include "features/ground_item/ground_item_service.h"
#include "features/player/player_inventory_service.h"

#include "core/application.h"

#include "shared/features/player/player_carry.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_disguise.h"
#include "shared/features/player/player_heal.h"
#include "shared/features/player/player_ledge.h"
#include "shared/features/player/player_movement_mode.h"
#include "shared/features/player/player_spawn.h"
#include "shared/features/player/player_teleport.h"
#include "shared/features/player/player_visibility.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/profiler.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <unordered_set>
#include <vector>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <cctype>
#include <iterator>
#include <utils/hooking/hooking.h>

#include "core/boot/agent_testing.h"

#include "features/book/book_service.h"
#include "features/player/player_female_body.h"
#include "features/dice/dice_module.h"
#include "features/siege/siege_module.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/entitymodule/c_rider_player_input.h"
#include "sdk/wh/entitymodule/c_actor_action_ladder.h"
#include "sdk/wh/entitymodule/s_ledge_grab_input.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/wh/entitymodule/c_player_input.h"
#include "sdk/wh/entitymodule/c_actor_action_carry_corpse.h"
#include "sdk/wh/entitymodule/c_actor_action_carry_item.h"
#include "sdk/cryengine/3dengine/c_mesh.h"
#include "sdk/cryengine/3dengine/c_skinning_info.h"
#include "sdk/cryengine/3dengine/i_chunk_file.h"
#include "sdk/cryengine/animation/c_default_skeleton.h"
#include "sdk/cryengine/animation/c_internal_database_info.h"
#include "sdk/cryengine/animation/character_manager.h"
#include "sdk/cryengine/animation/global_animation_header_caf.h"
#include "sdk/cryengine/system/c_xml_utils.h"
#include "sdk/cryengine/system/xml_node_ref.h"

#include "common/head_visibility.h"


namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "Player";
        // Matches the presentation streams on both peers: nothing a terminal
        // attack edge waits for takes longer than this to resolve.
        constexpr std::uint64_t kCombatAttackTerminalDeadlineMs = 5000;

        // How long an item made for a carry may wait for the carry to enter.
        // A queued action enters on the queue's next update, so this only
        // ever runs out for a carry the queue refused or dropped.
        constexpr MafiaNet::Time kCarryItemStartTimeoutMs = 3000;

        // How long a dropped carry item waits for the prop the server makes of
        // it. The prop's replica takes it over; one that never comes -- the
        // carrier had no room left on the ground -- leaves it to the sweep.
        constexpr MafiaNet::Time kCarryItemAdoptWindowMs = 6000;

    } // namespace

    // --- The tick ---------------------------------------------------------

    void PlayerService::QueueBodyDespawn(std::uint64_t networkId, SDK::EntityId entityId) {
        if (networkId == 0 && entityId == 0) {
            return;
        }

        std::scoped_lock lock(_pendingBodyDespawnsMutex);
        _pendingBodyDespawns.push_back(PendingBodyDespawn {networkId, entityId});
    }

    void PlayerService::DrainBodyDespawns() {
        std::vector<PendingBodyDespawn> pending;
        {
            std::scoped_lock lock(_pendingBodyDespawnsMutex);
            pending.swap(_pendingBodyDespawns);
        }

        auto *app = KCDC::Core::gApplication.get();
        if (app == nullptr) {
            // The session went, and the bodies went with the level it took.
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        for (const PendingBodyDespawn &despawn : pending) {
            app->GetPlayerMissileService().ForgetActor(despawn.networkId);
            app->GetEmoteService().ForgetActor(despawn.networkId);
            KCDC::Core::PresentationModule::Get().ForgetActor(despawn.networkId);
            app->GetHorseService().ForgetSeatsFor(despawn.entityId);

            // An EntityId carries the slot's salt, which the entity system
            // bumps whenever the slot is freed, so a recycled id stops
            // resolving and GetEntity is the whole identity test. A guid is
            // the wrong question: the engine mints one only for entities it
            // has to persist, so a spawned body has none.
            const bool sameNativeEntity = entitySystem != nullptr && entitySystem->GetEntity(despawn.entityId) != nullptr;
            if (!sameNativeEntity) {
                // A level change took the body before this tick settled it.
                Framework::Logging::GetLogger(kLogger)->debug("Body for networkId {} was already gone when its despawn was settled (entity {})", despawn.networkId, despawn.entityId);
                continue;
            }

            // A carry must not outlive either of its bodies; see Human::Despawn.
            ReleaseCarriesOf(despawn.entityId);
            ReleaseCarryBy(despawn.entityId);
            ReleaseItemCarryBy(despawn.entityId);

            app->GetActorSpawner().DespawnNow(despawn.entityId);
            Framework::Logging::GetLogger(kLogger)->info("Despawned the body of networkId {} (entity {})", despawn.networkId, despawn.entityId);
        }
    }

    void PlayerService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        RefreshLocalActor();

        replication->ForEach<Human>([](Human *human) {
            human->Frame();
        });

        // After the bodies, because the fence it stamps is read off the
        // owner's replica the loop above has just settled. LocalEvents ran
        // this from the same sweep, later, with the same tick behind it.
        FlushCombatAttackTerminal();

        SweepCarryItems();
    }

    void PlayerService::Reset() {
        ForgetLocalCombat();
        _bodySwapPending = false;

        // The next session's swings start from generation one, and a terminal
        // edge that never reached the wire is about a body that is gone.
        _nextCombatAttackGeneration = 1;
        ClearActiveCombatAttack();
        _pendingCombatAttackTerminal.reset();
        _pendingCombatAttackDeadline = 0;

        ForgetClimbs();
        ForgetCarries();
        ForgetItemCarries();
        _reportedGameOver = false;
        _deathStep        = DeathStep::Silent;

        // Both belong to the session that set them, and so does the body.
        _walkByDefault      = false;
        _sprintTookWalkMode = false;
        _walkEnforced       = false;
        _walkHoldActive     = false;
        _walkHoldWriting    = false;

        // The next session's body is a different body, so the comparison the
        // capture makes against this must not match anything. The dirty flags
        // are left set rather than cleared, which is how they are constructed:
        // nothing but a capture clears them, and the first capture of the next
        // session has to run.
        _localSoul.store(nullptr, std::memory_order_relaxed);
        _localSoulDirty.store(true, std::memory_order_relaxed);
        _equipmentChanged.store(true, std::memory_order_relaxed);
    }

    void PlayerService::SayDeathStep(DeathStep step, const char *reason) {
        if (_deathStep == step) {
            return;
        }
        _deathStep = step;
        Framework::Logging::GetLogger(kLogger)->debug("Local death path: {}", reason);
    }

    bool PlayerService::ReportLocalDeath() {
        auto *actor = KCDC::Common::LocalHumanActor();
        if (actor == nullptr || actor->GetSoul() == nullptr) {
            SayDeathStep(DeathStep::NoLocalBody, "there is no local body to report a death for");
            return false;
        }
        if (!actor->GetSoul()->IsDead()) {
            SayDeathStep(DeathStep::NotDead, "the local soul is alive, so this death is not this peer's");
            return false;
        }
        auto *human = KCDC::Features::Player::Human::FindLocal();
        if (human == nullptr) {
            SayDeathStep(DeathStep::NoOwnerReplica, "the local body is dead but this peer owns no replica to report it from");
            return false;
        }
        if (_reportedGameOver) {
            return true;
        }

        auto report = human->CaptureDeathReport();

        // The write that killed was reported on its way in, so what it said
        // is already waiting here.
        const KCDC::Core::LocalEvents::FatalHit fatal = KCDC::Core::gApplication->GetLocalEvents().TakeFatalHit();
        report.killerNetworkId                        = fatal.killerNetworkId;
        report.reason                                 = fatal.reason;

        if (!report.IsSane()) {
            // Still this peer's death, so the native sequence stays consumed --
            // but nothing will revive a body the server was never told about.
            SayDeathStep(DeathStep::ReportRefused, "the death snapshot was refused; the server has not been told and nothing can revive this body");
            return true;
        }

        _reportedGameOver = true;
        _deathStep        = DeathStep::Reported;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
        Framework::Logging::GetLogger(kLogger)->debug("Local death reported: networkId={} incarnation={} revision={} epoch={}", report.networkId, report.incarnation, report.deathRevision, report.epoch);
        return true;
    }

    void PlayerService::NotifySoulDied(SDK::wh::rpgmodule::C_Soul *soul) {
        // Every soul in the world arrives here, so the ones that are not this
        // body cost a pointer compare and say nothing.
        auto *actor = KCDC::Common::LocalHumanActor();
        if (soul == nullptr || actor == nullptr || actor->GetSoul() != soul) {
            return;
        }
        ReportLocalDeath();
    }

    bool PlayerService::NotifyGameOverRaised() {
        return ReportLocalDeath();
    }

    void PlayerService::NotifyRevived(SDK::wh::entitymodule::C_Actor *actor) {
        if (actor != KCDC::Common::LocalHumanActor()) {
            return;
        }

        if (_reportedGameOver) {
            if (auto *human = KCDC::Features::Player::Human::FindLocal()) {
                human->NativeRevived();
            }
            Framework::Logging::GetLogger(kLogger)->debug("Local death path: the local body was revived natively");
        }
        _reportedGameOver = false;
        _deathStep        = DeathStep::Silent;
    }

    bool PlayerService::RequestSpawn() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return false;
        }
        // This request precedes the ready barrier. Its downloaded replicas
        // already exist, but CoreModules' gameplay manager is still unset.
        auto *human = KCDC::Common::FindReplica<Human>([](Human *candidate) { return candidate->IsOwner(); }, net->GetReplicationManager());
        if (human == nullptr) {
            return false;
        }

        Shared::RPC::RequestSpawn request;
        request.networkId = human->GetNetworkID();
        // A client's only peer is the server.
        net->BroadcastRPC(request);
        Framework::Logging::GetLogger(kLogger)->debug("Asked the server where body {} goes", request.networkId);
        return true;
    }

    void PlayerService::Install() {
        // Releases are rare and come one at a time, so the walk of every
        // human is the right price for a pointer that never dangles.
        // A body can be released after the session has ended -- the engine
        // tears remote bodies down on its own schedule -- by which time
        // replication is gone and with it every Human that could point at it.
        KCDC::Features::Player::NativePlayerModule::Get().SetRemoteReleasedListener([](SDK::EntityId entityId) {
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Human>([entityId](Human *human) {
                    if (human->GetEntityId() == entityId) {
                        human->ForgetNativePlayer();
                    }
                });
            }
        });

        auto *net = Framework::CoreModules::GetNetworkPeer();

        net->RegisterRPC<Shared::RPC::RespawnPlayer>([](const Shared::RPC::RespawnPlayer &permission, MafiaNet::Packet *) {
            if (!permission.IsSane()) {
                return;
            }
            auto *replication = Framework::CoreModules::GetReplication();
            if (replication == nullptr) {
                return;
            }
            if (auto *human = replication->GetEntity<Human>(permission.networkId)) {
                human->Respawn(permission);
            }
        });

        net->RegisterRPC<Shared::RPC::SetPlayerAppearance>([](const Shared::RPC::SetPlayerAppearance &msg, MafiaNet::Packet *) {
            if (!msg.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Ignoring an appearance the component tree has no names for");
                return;
            }
            // The owner is the only peer that may publish its own body.
            auto *human = KCDC::Features::Player::Human::FindLocal();
            if (human == nullptr) {
                return;
            }
            if (!human->AdoptAppearance(msg.appearance)) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not adopt the appearance the server asked for");
            }
        });

        net->RegisterRPC<Shared::RPC::SetPlayerDisguise>([](const Shared::RPC::SetPlayerDisguise &msg, MafiaNet::Packet *) {
            if (!msg.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Ignoring a disguise the prop catalog has no mesh for");
                return;
            }
            // The owner is the only peer that may publish its own body.
            if (auto *human = KCDC::Features::Player::Human::FindLocal()) {
                human->AdoptDisguise(msg.disguise);
            }
        });

        net->RegisterRPC<Shared::RPC::SetPlayerVisible>([](const Shared::RPC::SetPlayerVisible &msg, MafiaNet::Packet *) {
            // The owner is the only peer that may publish its own body.
            if (auto *human = KCDC::Features::Player::Human::FindLocal()) {
                human->AdoptVisibility(msg.visible);
            }
        });

        net->RegisterRPC<Shared::RPC::SpawnPlayer>([](const Shared::RPC::SpawnPlayer &spawn, MafiaNet::Packet *) {
            auto *app = KCDC::Core::gApplication.get();
            if (!spawn.IsSane() || app == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("Ignoring a spawn the wire could not make sense of");
                return;
            }
            // Only a join waits on this answer; a session that is gone has nowhere to put it.
            if (!app->GetSessionLoadContext().active) {
                return;
            }

            std::uint64_t placementId = 0;
            if (spawn.placed) {
                TeleportRequest request;
                request.position      = SDK::Vec3 {spawn.x, spawn.y, spawn.z};
                request.rotation      = SDK::Quat {spawn.rotationX, spawn.rotationY, spawn.rotationZ, spawn.rotationW};
                request.hasRotation   = true;
                request.waitForGround = true;
                request.label         = "spawn";
                placementId           = app->GetPlayerTeleporter().QueueTeleport(request);
                Framework::Logging::GetLogger(kLogger)->info("Spawning the body at ({:.1f}, {:.1f}, {:.1f})", spawn.x, spawn.y, spawn.z);
            }
            else {
                Framework::Logging::GetLogger(kLogger)->info("The server named no spawn point; the body keeps where it is");
            }

            app->OnSessionSpawnAnswered(placementId);
        });

        net->RegisterRPC<Shared::RPC::SetPlayerMovementMode>([](const Shared::RPC::SetPlayerMovementMode &mode, MafiaNet::Packet *) {
            auto *app = KCDC::Core::gApplication.get();
            if (!mode.IsSane() || app == nullptr) {
                return;
            }

            auto &players = app->GetPlayerService();
            players.SetWalkByDefault(mode.walkByDefault);
            players.SetWalkEnforced(mode.walkEnforced);
        });

        // The gamemode refused a carry, or took the carried body away.
        net->RegisterRPC<Shared::RPC::PutDownCarried>([](const Shared::RPC::PutDownCarried &order, MafiaNet::Packet *) {
            auto *app = KCDC::Core::gApplication.get();
            if (order.IsSane() && app != nullptr) {
                app->GetPlayerService().PutDownLocalCarry(order.immediate);
            }
        });

        // The gamemode wants the local player to carry an item: a prop it
        // claimed for them, taken up with the game's pick-up, or one made
        // straight in their hands. A prop with no item here to take up --
        // not built yet on this machine -- is stood in for by a made one.
        net->RegisterRPC<Shared::RPC::CarryItem>([](const Shared::RPC::CarryItem &order, MafiaNet::Packet *) {
            auto *app   = KCDC::Core::gApplication.get();
            auto *actor = KCDC::Common::LocalHumanActor();
            if (!order.IsSane() || app == nullptr || actor == nullptr) {
                return;
            }
            auto &players = app->GetPlayerService();
            auto *prop    = order.groundItemNetworkId != 0 ? KCDC::Common::FindReplicaById<KCDC::Features::GroundItem::GroundItem>(order.groundItemNetworkId) : nullptr;
            if (prop != nullptr && players.StartGroundItemCarry(actor, prop)) {
                return;
            }
            if (!players.StartItemCarry(actor, order.itemClass, order.groundItemNetworkId)) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not start carrying item {:016x}{:016x}", order.itemClass.low, order.itemClass.high);
            }
        });

        // The gamemode refused an item carry, or wants it put down.
        net->RegisterRPC<Shared::RPC::PutDownCarriedItem>([](const Shared::RPC::PutDownCarriedItem &order, MafiaNet::Packet *) {
            auto *app = KCDC::Core::gApplication.get();
            if (order.IsSane() && app != nullptr) {
                app->GetPlayerService().PutDownLocalItemCarry(order.immediate, order.refused);
            }
        });

        net->RegisterRPC<Shared::RPC::SetPlayerResource>([](const Shared::RPC::SetPlayerResource &request, MafiaNet::Packet *) {
            if (!request.IsSane()) {
                return;
            }
            if (auto *human = KCDC::Features::Player::Human::FindLocal()) {
                human->SetResource(request);
            }
        });

        net->RegisterRPC<Shared::RPC::HealPlayer>([](const Shared::RPC::HealPlayer &heal, MafiaNet::Packet *) {
            if (!heal.IsSane()) {
                return;
            }
            if (auto *human = KCDC::Features::Player::Human::FindLocal()) {
                human->Heal(heal.health);
            }
        });

        net->RegisterRPC<Shared::RPC::TeleportPlayer>([](const Shared::RPC::TeleportPlayer &msg, MafiaNet::Packet *) {
            auto *app = KCDC::Core::gApplication.get();
            if (!msg.IsSane() || app == nullptr) {
                return;
            }

            TeleportRequest request;
            request.position = SDK::Vec3 {msg.x, msg.y, msg.z};
            request.label    = msg.label.empty() ? std::string("the server's request") : msg.label;
            app->GetPlayerTeleporter().QueueTeleport(request);
        });
    }

    // --- The local player's body ------------------------------------------

    void PlayerService::RefreshLocalActor() {
        auto *framework                   = SDK::IGameFramework::GetInstance();
        auto *localActor                  = framework != nullptr ? framework->GetClientActor() : nullptr;
        const SDK::EntityId actorEntityId = localActor != nullptr ? localActor->GetEntityId() : 0;
        if (_localCombat.actorEntityId == actorEntityId) {
            return;
        }
        const bool combatChanged   = _localCombat.fistsUp || _localCombat.guardRequest != 0 || _localCombat.combatZone != -1;
        _localCombat.actorEntityId = actorEntityId;
        _localCombat.fistsUp       = false;
        _localCombat.guardRequest  = 0;
        _localCombat.combatZone    = -1;
        if (combatChanged) {
            AdvanceLocalCombatRevision();
        }

        // A different body, so the hold is retaken against it. The pace
        // default came from the input constructor; the permissions could not,
        // because the actor was not live enough there to take them.
        _walkHoldActive = false;
        SettleWalkHold();
    }

    // --- Movement mode ----------------------------------------------------

    void PlayerService::SetWalkByDefault(bool walkByDefault) {
        if (_walkByDefault == walkByDefault) {
            return;
        }
        _walkByDefault = walkByDefault;

        // The rule arriving is an edge of its own: the body standing there
        // gets it now, not on some later frame that thinks to look.
        auto *player = SDK::wh::entitymodule::C_Player::GetInstance();
        ApplyWalkDefault(player != nullptr ? player->GetPlayerInput() : nullptr);
    }

    void PlayerService::ApplyWalkDefault(SDK::wh::entitymodule::C_PlayerInput *input) {
        // Walk is a floor on the movement request, not a stored speed, so
        // writing it mid-sprint clamps the sprint itself.
        if (_walkByDefault && input != nullptr && !input->IsSprinting()) {
            input->SetRunMode(false);
        }
    }

    void PlayerService::NotifySprintForcedJog(SDK::wh::entitymodule::C_PlayerInput *input, bool wasWalking) {
        if (input == nullptr) {
            return;
        }

        if (input->IsSprinting()) {
            // Latched: the handler runs every frame the key is held, and
            // only the first of those was the walk.
            _sprintTookWalkMode = _sprintTookWalkMode || wasWalking;
            return;
        }

        // Jog forced with no sprint to show for it. Nothing has read the byte
        // yet, so putting it back here means it was never jog.
        _sprintTookWalkMode = false;
        if (wasWalking) {
            ApplyWalkDefault(input);
        }
    }

    void PlayerService::NotifySprintEnded(SDK::wh::entitymodule::C_PlayerInput *input) {
        if (!_sprintTookWalkMode) {
            return;
        }
        _sprintTookWalkMode = false;
        ApplyWalkDefault(input);
    }

    void PlayerService::SetWalkEnforced(bool enforced) {
        if (_walkEnforced == enforced) {
            return;
        }
        _walkEnforced = enforced;
        SettleWalkHold();
    }

    void PlayerService::SettleWalkHold() {
        auto *player = SDK::wh::entitymodule::C_Player::GetInstance();
        if (player == nullptr) {
            // The saved permissions describe a body that no longer exists.
            _walkHoldActive = false;
            return;
        }

        if (_walkEnforced) {
            if (!_walkHoldActive) {
                _walkHoldActive     = true;
                _heldAllowRunWas    = player->IsRunAllowed();
                _heldAllowSprintWas = player->IsSprintAllowed();
            }
            // The guard keeps the interception from reading this write back
            // as somebody's intent and overwriting what was just saved.
            _walkHoldWriting = true;
            player->SetMovementRestriction(false, false);
            _walkHoldWriting = false;
            return;
        }

        if (_walkHoldActive) {
            _walkHoldActive = false;
            player->SetMovementRestriction(_heldAllowRunWas, _heldAllowSprintWas);
        }
    }

    void PlayerService::InterceptMovementRestriction(const SDK::wh::entitymodule::C_Actor *actor, bool &allowRun, bool &allowSprint) {
        if (!_walkHoldActive || _walkHoldWriting || actor == nullptr || actor != static_cast<const SDK::wh::entitymodule::C_Actor *>(SDK::wh::entitymodule::C_Player::GetInstance())) {
            return;
        }

        // Keep what this caller wanted: it is what the body gets back when
        // the rule is lifted.
        _heldAllowRunWas    = allowRun;
        _heldAllowSprintWas = allowSprint;
        allowRun            = false;
        allowSprint         = false;
    }

    // --- Combat edges -----------------------------------------------------

    void PlayerService::AdvanceLocalCombatRevision() {
        _localCombat.combatRevision = Shared::Entities::HumanEntity::AdvanceCombatRevision(_localCombat.combatRevision);
    }

    void PlayerService::NotifyCombatStance(SDK::EntityId actorEntityId, bool fistsUp) {
        // The first combat event may precede the next local-body refresh.
        if (actorEntityId != _localCombat.actorEntityId) {
            RefreshLocalActor();
        }
        if (actorEntityId == 0 || actorEntityId != _localCombat.actorEntityId) {
            return;
        }

        const std::uint8_t nextGuardRequest = fistsUp ? _localCombat.guardRequest : 0;
        const std::int32_t nextCombatZone   = fistsUp ? _localCombat.combatZone : -1;
        if (_localCombat.fistsUp == fistsUp && _localCombat.guardRequest == nextGuardRequest && _localCombat.combatZone == nextCombatZone) {
            return;
        }

        _localCombat.fistsUp      = fistsUp;
        _localCombat.guardRequest = nextGuardRequest;
        _localCombat.combatZone   = nextCombatZone;
        AdvanceLocalCombatRevision();
    }

    void PlayerService::NotifyCombatGuard(SDK::EntityId actorEntityId, std::uint8_t guardRequest) {
        if (actorEntityId != _localCombat.actorEntityId) {
            RefreshLocalActor();
        }
        if (actorEntityId == 0 || actorEntityId != _localCombat.actorEntityId) {
            return;
        }

        if (_localCombat.guardRequest != guardRequest) {
            _localCombat.guardRequest = guardRequest;
            AdvanceLocalCombatRevision();
        }
    }

    void PlayerService::NotifyCombatZone(SDK::EntityId actorEntityId, std::int32_t zoneId) {
        if (actorEntityId != _localCombat.actorEntityId) {
            RefreshLocalActor();
        }
        if (actorEntityId == 0 || actorEntityId != _localCombat.actorEntityId) {
            return;
        }

        if (_localCombat.combatZone != zoneId) {
            _localCombat.combatZone = zoneId;
            AdvanceLocalCombatRevision();
        }
    }

    void PlayerService::NotifyCombatStanceLost(SDK::EntityId actorEntityId) {
        // LeaveCombatMode does not call RequestFightingStance(false); capture
        // its fists-down transition here.
        if (actorEntityId != _localCombat.actorEntityId) {
            RefreshLocalActor();
        }
        if (actorEntityId != 0 && actorEntityId == _localCombat.actorEntityId) {
            NotifyCombatStance(actorEntityId, false);
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (actorEntityId == 0 || replication == nullptr) {
            return;
        }

        // Only a body we drive; the local player's stance is his own engine's.
        replication->ForEach<Human>([actorEntityId](Human *human) {
            if (human->IsOwner() || human->GetEntityId() != actorEntityId) {
                return;
            }

            human->NoteStanceLost();
        });
    }

    void PlayerService::ForgetLocalCombat() {
        _localCombat = LocalCombat {};
    }

    // --- Accepted swings --------------------------------------------------

    bool PlayerService::CaptureLocalActorFence(std::uint32_t &incarnation, std::uint8_t &epoch, std::uint32_t &combatRevision) const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return false;
        }
        bool foundOwner = false;
        replication->ForEach<Human>([&](Human *human) {
            if (!foundOwner && human->IsOwner() && human->HasCapturedLocalSoul()) {
                incarnation = human->soul.incarnation;
                epoch       = human->stateEpoch;
                foundOwner  = true;
            }
        });
        if (!foundOwner || incarnation == 0) {
            return false;
        }
        combatRevision = _localCombat.combatRevision;
        return true;
    }

    bool PlayerService::CompleteCombatAttackFence(Shared::RPC::CombatAttack &attack) const {
        const bool captured = CaptureLocalActorFence(attack.incarnation, attack.epoch, attack.combatRevision);
        Framework::Logging::GetLogger(kLogger)->debug("Local attack {} fenced at combatRevision={} incarnation={} epoch={}", attack.PhaseName(), attack.combatRevision, attack.incarnation, attack.epoch);
        return captured;
    }

    void PlayerService::AdvanceCombatAttackGeneration() {
        ++_nextCombatAttackGeneration;
        if (_nextCombatAttackGeneration == 0) {
            ++_nextCombatAttackGeneration;
        }
    }

    void PlayerService::ClearActiveCombatAttack() {
        _activeCombatAttackGeneration = 0;
        _activeCombatAttackInputClass = -1;
    }

    void PlayerService::HoldCombatAttackTerminal(const Shared::RPC::CombatAttack &attack) {
        _pendingCombatAttackTerminal = attack;
        _pendingCombatAttackDeadline = MafiaNet::GetTime() + kCombatAttackTerminalDeadlineMs;
    }

    void PlayerService::FlushCombatAttackTerminal() {
        if (!_pendingCombatAttackTerminal) {
            return;
        }

        Shared::RPC::CombatAttack attack = *_pendingCombatAttackTerminal;
        if (!CompleteCombatAttackFence(attack) || !KCDC::Core::PresentationModule::Get().QueueLocalCombatAttack(attack)) {
            if (MafiaNet::GetTime() < _pendingCombatAttackDeadline) {
                return;
            }
            Framework::Logging::GetLogger(kLogger)->warn("Abandoned local attack {} for generation {} after {} ms; further swings are reportable again", attack.PhaseName(), attack.attackGeneration, kCombatAttackTerminalDeadlineMs);
        }
        if (_activeCombatAttackGeneration == attack.attackGeneration) {
            ClearActiveCombatAttack();
        }
        _pendingCombatAttackTerminal.reset();
        _pendingCombatAttackDeadline = 0;
    }

    void PlayerService::NotifyCombatAttackBegan(std::int32_t inputClassId, std::int32_t zoneId) {
        if (inputClassId < 0) {
            return;
        }

        if (_activeCombatAttackGeneration != 0) {
            Framework::Logging::GetLogger(kLogger)->warn("Accepted a new local attack Press while generation {} remained active; canceling the previous generation first", _activeCombatAttackGeneration);
            NotifyCombatAttackCanceled();
            if (_activeCombatAttackGeneration != 0) {
                return;
            }
        }

        Shared::RPC::CombatAttack attack;
        attack.phase             = Shared::RPC::CombatAttack::Press;
        attack.attackGeneration  = _nextCombatAttackGeneration;
        attack.inputClassId      = inputClassId;
        attack.zoneId            = zoneId >= -1 && zoneId <= 0x7FFF ? static_cast<std::int16_t>(zoneId) : static_cast<std::int16_t>(-1);
        attack.opponentNetworkId = KCDC::Core::gApplication->GetPlayerDuelService().LocalOpponentNetworkId();
        KCDC::Core::gApplication->GetPlayerDuelService().NoteLocalSwingTarget(attack.opponentNetworkId);
        if (!CompleteCombatAttackFence(attack) || !KCDC::Core::PresentationModule::Get().QueueLocalCombatAttack(attack)) {
            Framework::Logging::GetLogger(kLogger)->warn("Could not queue accepted local attack Press: generation={}, inputClass={}, zone={}", attack.attackGeneration, inputClassId, zoneId);
            return;
        }

        _activeCombatAttackGeneration = attack.attackGeneration;
        _activeCombatAttackInputClass = attack.inputClassId;
        AdvanceCombatAttackGeneration();
    }

    void PlayerService::NotifyCombatAttackCommitted(const SDK::wh::combatmodule::S_CombatActionAttackDataDBData *attackData, std::int32_t zoneId, std::uint8_t candidateFlag0, std::uint8_t candidateFlag1, std::uint8_t contextFlag0, std::uint8_t contextFlag1,
        std::uint32_t stateMode, float attackSpeed) {
        if (_activeCombatAttackGeneration == 0 || attackData == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Ignored accepted local attack Commit without an active Press or attack row: generation={}, row={}", _activeCombatAttackGeneration, static_cast<const void *>(attackData));
            return;
        }

        SDK::wh::combatmodule::C_CombatActionAttackDatabase *database = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance();
        const std::uint32_t attackRowIndex                            = database->GetRowIndex(attackData);
        if (attackRowIndex == Shared::RPC::CombatAttack::kInvalidAttackRowIndex) {
            Framework::Logging::GetLogger(kLogger)->error("Accepted local attack action does not point into the process attack database: generation={}, row={}", _activeCombatAttackGeneration, static_cast<const void *>(attackData));
            NotifyCombatAttackCanceled();
            return;
        }

        Shared::RPC::CombatAttack attack;
        attack.phase             = Shared::RPC::CombatAttack::Commit;
        attack.attackGeneration  = _activeCombatAttackGeneration;
        attack.inputClassId      = _activeCombatAttackInputClass;
        attack.zoneId            = zoneId >= -1 && zoneId <= 0x7FFF ? static_cast<std::int16_t>(zoneId) : static_cast<std::int16_t>(-1);
        attack.attackRowIndex    = attackRowIndex;
        attack.candidateFlag0    = candidateFlag0;
        attack.candidateFlag1    = candidateFlag1;
        attack.contextFlag0      = contextFlag0;
        attack.contextFlag1      = contextFlag1;
        attack.stateMode         = stateMode;
        attack.attackSpeed       = attackSpeed;
        attack.windowMs          = Shared::RPC::CombatSwingWindowMs(attackData->GetTimeToHit(), attackData->GetTimeToWithdraw(), attackSpeed);
        attack.opponentNetworkId = KCDC::Core::gApplication->GetPlayerDuelService().LocalOpponentNetworkId();
        KCDC::Core::gApplication->GetPlayerDuelService().NoteLocalSwingTarget(attack.opponentNetworkId);
        KCDC::Core::gApplication->GetPlayerDuelService().NoteLocalSwing(attack.attackGeneration);
        if (!CompleteCombatAttackFence(attack) || !KCDC::Core::PresentationModule::Get().QueueLocalCombatAttack(attack)) {
            HoldCombatAttackTerminal(attack);
            Framework::Logging::GetLogger(kLogger)->warn("Could not queue accepted local attack Commit: generation={}, row={}", attack.attackGeneration, attack.attackRowIndex);
            return;
        }
        ClearActiveCombatAttack();
    }

    void PlayerService::NotifyCombatAttackCanceled() {
        if (_activeCombatAttackGeneration == 0 || _pendingCombatAttackTerminal) {
            return;
        }

        Shared::RPC::CombatAttack attack;
        attack.phase            = Shared::RPC::CombatAttack::Cancel;
        attack.attackGeneration = _activeCombatAttackGeneration;
        if (!CompleteCombatAttackFence(attack) || !KCDC::Core::PresentationModule::Get().QueueLocalCombatAttack(attack)) {
            HoldCombatAttackTerminal(attack);
            Framework::Logging::GetLogger(kLogger)->warn("Could not queue local attack Cancel: generation={}", attack.attackGeneration);
            return;
        }
        ClearActiveCombatAttack();
    }

    // --- Traversal --------------------------------------------------------

    void PlayerService::NotifyJumpStarted(SDK::EntityId actorEntityId) {
        const SDK::EntityId localActorEntityId = KCDC::Common::LocalActorEntityId();
        if (actorEntityId == 0 || actorEntityId != localActorEntityId) {
            return;
        }

        // Counted rather than sent: the pose carries the count, so a receiver
        // starts the jump when the body it draws reaches the takeoff.
        _localJumpCount.fetch_add(1, std::memory_order_relaxed);
    }

    void PlayerService::NotifySlowdownPressed(const SDK::wh::entitymodule::C_RiderPlayerInput *riderInput) {
        // A remote rider's copy is handed its owner's presses through the
        // same handler; only this machine's player is counted.
        const SDK::EntityId localActorEntityId = KCDC::Common::LocalActorEntityId();
        if (localActorEntityId == 0 || riderInput->GetPlayer()->GetEntityId() != localActorEntityId) {
            return;
        }
        _localSlowdownCount.fetch_add(1, std::memory_order_relaxed);
    }

    void PlayerService::NotifyLedgeGrabStarted(SDK::EntityId actorEntityId, const SDK::wh::entitymodule::S_LedgeGrabInput &input) {
        const SDK::EntityId localActorEntityId = KCDC::Common::LocalActorEntityId();
        if (actorEntityId == 0 || actorEntityId != localActorEntityId) {
            return;
        }

        Shared::RPC::PlayerLedgeGrab grab;
        grab.jumpRequested   = input.jumpRequested;
        grab.crouchRequested = input.crouchRequested;
        if (!CaptureLocalActorFence(grab.incarnation, grab.epoch, grab.combatRevision)) {
            return;
        }
        if (!KCDC::Core::PresentationModule::Get().QueueLocalLedgeGrab(grab)) {
            Framework::Logging::GetLogger(kLogger)->warn("Could not queue the local player's ledge grab: incarnation={}, epoch={}, combatRevision={}", grab.incarnation, grab.epoch, grab.combatRevision);
        }
    }

    // --- Climbs -----------------------------------------------------------

    SDK::EntityId PlayerService::ClimberOf(const SDK::wh::entitymodule::C_ActorActionLadder *action) {
        SDK::wh::entitymodule::C_ActionActor *queue = action != nullptr ? action->GetActionActor() : nullptr;
        SDK::wh::entitymodule::C_Actor *actor       = queue != nullptr ? queue->GetActor() : nullptr;
        return actor != nullptr ? actor->GetEntityId() : 0;
    }

    void PlayerService::NotifyLadderEntered(SDK::wh::entitymodule::C_ActorActionLadder *action) {
        const SDK::EntityId climber = ClimberOf(action);
        if (climber == 0) {
            return;
        }

        // Assigned, so a re-grab before the first action exits keeps one entry.
        _ladderActions[climber] = action;
    }

    void PlayerService::NotifyLadderExited(SDK::wh::entitymodule::C_ActorActionLadder *action) {
        const auto entry = _ladderActions.find(ClimberOf(action));

        // Only the action that is leaving clears the entry, so an exit after
        // a re-grab does not erase the climb that replaced it.
        if (entry != _ladderActions.end() && entry->second == action) {
            _ladderActions.erase(entry);
        }
    }

    SDK::wh::entitymodule::C_ActorActionLadder *PlayerService::LadderActionFor(SDK::EntityId actorEntityId) const {
        const auto entry = _ladderActions.find(actorEntityId);
        return entry != _ladderActions.end() ? entry->second : nullptr;
    }

    bool PlayerService::IsClimberAhead(SDK::EntityId climber, float direction) const {
        const auto self = _ladderActions.find(climber);
        if (self == _ladderActions.end()) {
            return false;
        }
        constexpr float kReachRungs = 8.0F;
        const float at              = self->second->GetRungPosition();
        return std::any_of(_ladderActions.begin(), _ladderActions.end(), [&](const auto &other) {
            const float ahead = (other.second->GetRungPosition() - at) * (direction > 0.0F ? 1.0F : -1.0F);
            return other.first != climber && other.second->GetLadderEntityId() == self->second->GetLadderEntityId() && ahead > 0.0F && ahead <= kReachRungs;
        });
    }

    void PlayerService::ForgetClimbs() {
        _ladderActions.clear();
    }

    // --- Carrying ---------------------------------------------------------

    SDK::wh::entitymodule::C_Actor *PlayerService::CarrierOf(const SDK::wh::entitymodule::C_ActorActionCarryCorpse *action) {
        SDK::wh::entitymodule::C_ActionActor *queue = action != nullptr ? action->GetActionActor() : nullptr;
        return queue != nullptr ? queue->GetActor() : nullptr;
    }

    void PlayerService::NotifyCarryEntered(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action) {
        SDK::wh::entitymodule::C_Actor *carrier = CarrierOf(action);
        if (carrier == nullptr) {
            return;
        }

        // Assigned, so a re-grab before the first action exits keeps one entry.
        const SDK::EntityId carrierEntityId = carrier->GetEntityId();
        _carryActions[carrierEntityId]      = action;

        if (carrierEntityId == KCDC::Common::LocalActorEntityId()) {
            KCDC::Core::gApplication->GetLocalEvents().LocalCarryStarted(action->GetTargetEntityId());
        }
    }

    void PlayerService::NotifyCarryExited(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action) {
        SDK::wh::entitymodule::C_Actor *carrier = CarrierOf(action);
        if (carrier == nullptr) {
            return;
        }

        // Only the action that is leaving clears the entry, so an exit after
        // a re-grab does not erase the carry that replaced it.
        const SDK::EntityId carrierEntityId = carrier->GetEntityId();
        const auto entry                    = _carryActions.find(carrierEntityId);
        if (entry == _carryActions.end() || entry->second != action) {
            return;
        }
        _carryActions.erase(entry);

        if (carrierEntityId == KCDC::Common::LocalActorEntityId()) {
            KCDC::Core::gApplication->GetLocalEvents().LocalCarryEnded();
        }
    }

    void PlayerService::ReleaseCarry(SDK::wh::entitymodule::C_Actor *carrier) {
        SDK::wh::entitymodule::I_ActorCarryCorpse *carry = carrier != nullptr ? carrier->FindCarryCorpse() : nullptr;
        if (carry == nullptr || !carry->HasTarget()) {
            return;
        }
        Framework::Logging::GetLogger(kLogger)->info("Entity {} lets go of entity {} at once", carrier->GetEntityId(), carry->GetTargetEntityId());

        // The stop goes through the action's queue (0x1809CD780), which runs
        // the action's Exit -- and with it EndCarrying -- when it can end the
        // action there and then. EndCarrying is called regardless, because the
        // body is about to go and cannot wait for a queue that defers; it finds
        // nothing bound when Exit already ran. Stopped first: EndCarrying
        // forgets the running action it would otherwise have to stop.
        carry->StopCarryAction();
        carry->EndCarrying();
    }

    void PlayerService::ReleaseCarriesOf(SDK::EntityId targetEntityId) {
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        if (targetEntityId == 0 || actorSystem == nullptr) {
            return;
        }

        // Collected first: releasing runs the engine's own teardown, which
        // must not run under an iteration of the map its hooks write.
        std::vector<SDK::EntityId> carriers;
        for (const auto &[carrierEntityId, action] : _carryActions) {
            if (action->GetTargetEntityId() == targetEntityId) {
                carriers.push_back(carrierEntityId);
            }
        }
        for (const SDK::EntityId carrierEntityId : carriers) {
            ReleaseCarry(static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(carrierEntityId)));
        }
    }

    void PlayerService::ReleaseCarryBy(SDK::EntityId carrierEntityId) {
        // Asked of the carrier's own half, which is what holds the body; the
        // register is only the index from a body to whoever holds it.
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        if (carrierEntityId != 0 && actorSystem != nullptr) {
            ReleaseCarry(static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(carrierEntityId)));
        }
    }

    void PlayerService::PutDownLocalCarry(bool immediate) {
        auto *actor                                      = KCDC::Common::LocalHumanActor();
        SDK::wh::entitymodule::I_ActorCarryCorpse *carry = actor != nullptr ? actor->FindCarryCorpse() : nullptr;
        if (carry == nullptr || !carry->HasTarget()) {
            return;
        }
        // Both put-downs only act on a body already on the shoulder
        // (BeginPutDown, 0x1828601AC, requires state 5); one still being
        // picked up is let go outright.
        if (!carry->IsCarrying()) {
            ReleaseCarry(actor);
            return;
        }
        if (immediate) {
            carry->RequestPutDownImmediate();
        }
        else {
            carry->RequestPutDown();
        }
    }

    void PlayerService::ForgetCarries() {
        _carryActions.clear();
    }

    // --- Carrying an item ---------------------------------------------------

    SDK::wh::entitymodule::C_Actor *PlayerService::CarrierOf(const SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        SDK::wh::entitymodule::C_ActionActor *queue = action != nullptr ? action->GetActionActor() : nullptr;
        return queue != nullptr ? queue->GetActor() : nullptr;
    }

    Shared::RPC::ItemClassId PlayerService::CarriedItemClassOf(const SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        // Asked of the item manager rather than of a holder: the item is in
        // the world or a pile before the pick-up and in the action after it,
        // and only the manager answers for all three.
        SDK::wh::entitymodule::C_Item *item = action != nullptr ? KCDC::Features::GroundItem::FindItem(action->GetItemId().value) : nullptr;
        if (item == nullptr) {
            return {};
        }
        const SDK::wh::entitymodule::S_ItemClassGUID itemClass = item->InvokeGetItemClassRaw();
        return Shared::RPC::ItemClassId {itemClass.m_low, itemClass.m_high};
    }

    PlayerService::CarryItemRecord *PlayerService::FindCarryItemRecord(std::uint64_t itemWuid) {
        const auto found = std::find_if(_carryItems.begin(), _carryItems.end(), [itemWuid](const CarryItemRecord &record) { return record.itemWuid == itemWuid; });
        return found != _carryItems.end() ? &*found : nullptr;
    }

    void PlayerService::PrepareItemCarryEnter(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        _enteringItemCarry = action;

        const CarryItemRecord *record = FindCarryItemRecord(action->GetItemId().value);
        if (record != nullptr && record->instant && action->GetPhase() == SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::New) {
            action->SetPhase(SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::PickingUp);
        }
    }

    void PlayerService::NotifyItemCarryEntered(SDK::wh::entitymodule::C_ActorActionCarryItem *action, bool entered) {
        _enteringItemCarry = nullptr;
        SDK::wh::entitymodule::C_Actor *carrier = entered ? CarrierOf(action) : nullptr;
        if (carrier == nullptr) {
            return;
        }

        // Players' bodies only: the local one and the copies.
        const SDK::EntityId carrierEntityId = carrier->GetEntityId();
        const bool local                    = carrierEntityId == KCDC::Common::LocalActorEntityId();
        if (!local && KCDC::Common::FindReplica<Human>([carrierEntityId](Human *human) { return !human->IsOwner() && human->GetEntityId() == carrierEntityId; }) == nullptr) {
            return;
        }

        // Assigned, so a new carry before the last one exits keeps one entry.
        const Shared::RPC::ItemClassId itemClass = CarriedItemClassOf(action);
        _itemCarryActions[carrierEntityId]       = ItemCarry {action, itemClass};

        const std::uint64_t itemWuid = action->GetItemId().value;
        CarryItemRecord *record      = FindCarryItemRecord(itemWuid);
        if (record == nullptr && local) {
            // A carry the game started by itself: a pile's item, or a prop
            // whose class the game scripts as `CarryableItem` -- its own
            // prompt carries those without asking anybody. A prop's item is
            // the prop's no more, and the server is told which prop it was.
            KCDC::Features::GroundItem::GroundItem *prop = KCDC::Features::GroundItem::GroundItemService::FindPropByNativeItem(itemWuid);
            if (prop != nullptr) {
                prop->ReleaseNativeForCarry();
            }
            _carryItems.push_back(CarryItemRecord {itemWuid, itemClass, carrierEntityId, prop != nullptr ? prop->GetNetworkID() : 0});
            record = &_carryItems.back();
        }
        if (record != nullptr) {
            record->carried = true;
        }

        if (local) {
            KCDC::Core::gApplication->GetLocalEvents().LocalItemCarryStarted(itemClass, record != nullptr ? record->groundItemNetworkId : 0);
        }
    }

    void PlayerService::NotifyItemCarryExited(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        // By now the item is wherever the carry left it: dropped back into
        // the world by the put-down's drop event or by Exit, or deposited in
        // a pile, which owns it from then on.
        auto *world                         = KCDC::Features::GroundItem::WorldInventory();
        const std::uint64_t itemWuid        = action->GetItemId().value;
        SDK::wh::entitymodule::C_Item *item = KCDC::Features::GroundItem::FindItem(itemWuid);
        const bool dropped                  = item != nullptr && world != nullptr && item->GetOwner() == world;
        bool becomesProp                    = false;
        if (CarryItemRecord *record = FindCarryItemRecord(itemWuid)) {
            auto *prop = record->refused ? KCDC::Common::FindReplicaById<KCDC::Features::GroundItem::GroundItem>(record->groundItemNetworkId) : nullptr;
            if (prop != nullptr && dropped) {
                // Refused: the prop it came from never left, so it gets its
                // item back rather than a second prop being made of it.
                prop->RebindNative(item);
                std::erase_if(_carryItems, [itemWuid](const CarryItemRecord &candidate) { return candidate.itemWuid == itemWuid; });
            }
            else if (dropped) {
                record->carried  = false;
                record->dropped  = true;
                record->removeAt = MafiaNet::GetTime() + kCarryItemAdoptWindowMs;
                becomesProp      = !record->refused;
            }
            else {
                std::erase_if(_carryItems, [itemWuid](const CarryItemRecord &candidate) { return candidate.itemWuid == itemWuid; });
            }
        }

        // Only the action that is leaving clears the entry; nothing is read
        // through it, so this holds for an NPC's carry that was never kept.
        const auto entry = std::find_if(_itemCarryActions.begin(), _itemCarryActions.end(), [action](const auto &candidate) { return candidate.second.action == action; });
        if (entry == _itemCarryActions.end()) {
            return;
        }
        const SDK::EntityId carrierEntityId = entry->first;
        _itemCarryActions.erase(entry);

        if (carrierEntityId == KCDC::Common::LocalActorEntityId()) {
            // Where the hands let it go, so the prop is made around this very
            // item rather than at a pose the server guesses and moves it to.
            auto *pickable = becomesProp ? item->GetPickableItem() : nullptr;
            auto *entity   = pickable != nullptr ? pickable->GetEntity() : nullptr;
            if (entity != nullptr) {
                const glm::vec3 dropPosition = KCDC::Common::ToGlm(entity->GetWorldPos());
                const glm::quat dropRotation = KCDC::Common::ToGlm(entity->GetWorldRotation());
                KCDC::Core::gApplication->GetLocalEvents().LocalItemCarryEnded(becomesProp, &dropPosition, &dropRotation);
            }
            else {
                KCDC::Core::gApplication->GetLocalEvents().LocalItemCarryEnded(becomesProp, nullptr, nullptr);
            }
        }
    }

    void PlayerService::NotifyItemPutDownStarted(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        const auto entry = std::find_if(_itemCarryActions.begin(), _itemCarryActions.end(), [action](const auto &candidate) { return candidate.second.action == action; });
        if (entry != _itemCarryActions.end() && entry->first == KCDC::Common::LocalActorEntityId()) {
            KCDC::Core::gApplication->GetLocalEvents().LocalItemPutDownStarted();
        }
    }

    SDK::wh::entitymodule::C_ActorActionCarryItem *PlayerService::ItemCarryActionFor(SDK::EntityId carrierEntityId) const {
        const auto entry = _itemCarryActions.find(carrierEntityId);
        return entry != _itemCarryActions.end() ? entry->second.action : nullptr;
    }

    Shared::RPC::ItemClassId PlayerService::ItemCarryClassFor(SDK::EntityId carrierEntityId) const {
        const auto entry = _itemCarryActions.find(carrierEntityId);
        return entry != _itemCarryActions.end() ? entry->second.itemClass : Shared::RPC::ItemClassId {};
    }

    Shared::Game::ItemCarryState PlayerService::LocalItemCarryState() const {
        Shared::Game::ItemCarryState state;
        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        const Human *owner                    = KCDC::Features::Player::Human::FindLocal();
        if (actor == nullptr || owner == nullptr) {
            return state;
        }
        const SDK::EntityId entityId                         = actor->GetEntityId();
        SDK::wh::entitymodule::I_ActorCarryCorpse *body       = actor->FindCarryCorpse();
        state.canAct                                         = owner->soul.CanAct();
        state.mounted                                        = KCDC::Core::gApplication->GetHorseService().SeatOf(entityId) != 0 || KCDC::Features::Cart::CartService::Get().IsInCart(entityId);
        state.carryingBody                                   = body != nullptr && body->HasTarget();
        state.carryingItem                                   = ItemCarryActionFor(entityId) != nullptr;
        return state;
    }

    bool PlayerService::IsItemCarryHolder(const void *holder) const {
        if (holder == nullptr) {
            return false;
        }
        if (_enteringItemCarry != nullptr && _enteringItemCarry->GetItemHolder() == holder) {
            return true;
        }
        return std::any_of(_itemCarryActions.begin(), _itemCarryActions.end(), [holder](const auto &entry) { return entry.second.action->GetItemHolder() == holder; });
    }

    bool PlayerService::IsCarriedItem(std::uint64_t itemWuid) const {
        if (_enteringItemCarry != nullptr && _enteringItemCarry->GetItemId().value == itemWuid) {
            return true;
        }
        return std::any_of(_itemCarryActions.begin(), _itemCarryActions.end(), [itemWuid](const auto &entry) { return entry.second.action->GetItemId().value == itemWuid; });
    }

    bool PlayerService::IsCarryingItem(SDK::EntityId carrierEntityId, const Shared::RPC::ItemClassId &itemClass) const {
        return ItemCarryActionFor(carrierEntityId) != nullptr && ItemCarryClassFor(carrierEntityId) == itemClass;
    }

    bool PlayerService::StartItemCarry(SDK::wh::entitymodule::C_Actor *actor, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        auto *world = KCDC::Features::GroundItem::WorldInventory();
        auto *queue = actor != nullptr ? actor->GetActionActor() : nullptr;
        if (world == nullptr || queue == nullptr || !itemClass.IsValid()) {
            return false;
        }

        // Exactly one item, or none: a split creation is no item to carry.
        const std::vector<SDK::wh::entitymodule::C_Item *> created = KCDC::Features::GroundItem::CreateWorldItems(itemClass, 1);
        SDK::wh::entitymodule::C_Item *item                        = created.size() == 1 ? created.front() : nullptr;
        if (item == nullptr) {
            for (SDK::wh::entitymodule::C_Item *extra : created) {
                world->RemoveItem(extra);
            }
            Framework::Logging::GetLogger(kLogger)->error("Made no single item of class {:016x}{:016x} for entity {} to carry", itemClass.low, itemClass.high, actor->GetEntityId());
            return false;
        }

        // Never put on the ground: PrepareItemCarryEnter has the action skip
        // its pick-up, and the hand attachment spawns the item's entity where
        // it belongs.
        const std::uint64_t itemWuid = item->GetId();
        _carryItems.push_back(CarryItemRecord {itemWuid, itemClass, actor->GetEntityId(), groundItemNetworkId, true, false, false, false, MafiaNet::GetTime() + kCarryItemStartTimeoutMs});
        if (!RequestCarry(queue, itemWuid)) {
            // Nothing reached a hand, so nothing is on the ground to settle.
            Framework::Logging::GetLogger(kLogger)->info("Entity {} could not take up item {}: its action queue refused or failed the carry", actor->GetEntityId(), itemWuid);
            if (CarryItemRecord *record = FindCarryItemRecord(itemWuid)) {
                record->removeAt = 0;
            }
            return false;
        }

        Framework::Logging::GetLogger(kLogger)->info("Entity {} takes item {} of class {:016x}{:016x} into its hands", actor->GetEntityId(), itemWuid, itemClass.low, itemClass.high);
        return true;
    }

    bool PlayerService::StartGroundItemCarry(SDK::wh::entitymodule::C_Actor *actor, KCDC::Features::GroundItem::GroundItem *prop) {
        auto *queue = actor != nullptr ? actor->GetActionActor() : nullptr;
        if (queue == nullptr || prop == nullptr) {
            return false;
        }

        // The prop's own item, lying where the prop is: the pick-up reaches
        // for it there, which is the game's own `CarryableItem` pick-up.
        SDK::wh::entitymodule::C_Item *item = prop->ReleaseNativeForCarry();
        if (item == nullptr) {
            return false;
        }
        const std::uint64_t itemWuid = item->GetId();
        _carryItems.push_back(CarryItemRecord {itemWuid, prop->item.itemClass, actor->GetEntityId(), prop->GetNetworkID(), false, false, false, false, MafiaNet::GetTime() + kCarryItemStartTimeoutMs});
        if (RequestCarry(queue, itemWuid)) {
            Framework::Logging::GetLogger(kLogger)->info("Entity {} picks up ground item {}", actor->GetEntityId(), prop->GetNetworkID());
            return true;
        }

        // The game has no pick-up clip for every kind it carries -- the stone
        // basket's fails in BeginItemPickupAnimation -- so the prop's own item
        // goes straight into the hands instead, as a made one would.
        if (CarryItemRecord *record = FindCarryItemRecord(itemWuid)) {
            record->instant = true;
        }
        if (RequestCarry(queue, itemWuid)) {
            Framework::Logging::GetLogger(kLogger)->info("Entity {} takes ground item {} straight into its hands: the game has no pick-up for it", actor->GetEntityId(), prop->GetNetworkID());
            return true;
        }

        Framework::Logging::GetLogger(kLogger)->info("Entity {} could not take up ground item {}: its action queue refused or failed the carry", actor->GetEntityId(), prop->GetNetworkID());
        std::erase_if(_carryItems, [itemWuid](const CarryItemRecord &record) { return record.itemWuid == itemWuid; });
        prop->RebindNative(item);
        return false;
    }

    bool PlayerService::RequestCarry(SDK::wh::entitymodule::C_ActionActor *queue, std::uint64_t itemWuid) {
        const SDK::ScopedCryString noTag("");
        SDK::wh::entitymodule::S_CarryItemActionParams params {};
        params.item         = SDK::wh::framework::WUID {itemWuid};
        params.animationTag = noTag.Get();
        params.pack         = false;
        return queue->RequestCarryItem(params);
    }

    SDK::wh::entitymodule::C_Item *PlayerService::TakeDroppedCarryItem(SDK::EntityId carrierEntityId, const Shared::RPC::ItemClassId &itemClass) {
        auto *world       = KCDC::Features::GroundItem::WorldInventory();
        const auto record = std::find_if(_carryItems.begin(), _carryItems.end(), [carrierEntityId, &itemClass](const CarryItemRecord &candidate) { return candidate.dropped && candidate.carrierEntityId == carrierEntityId && candidate.itemClass == itemClass; });
        if (record == _carryItems.end() || world == nullptr) {
            return nullptr;
        }
        SDK::wh::entitymodule::C_Item *item = KCDC::Features::GroundItem::FindItem(record->itemWuid);
        _carryItems.erase(record);
        return item != nullptr && item->GetOwner() == world ? item : nullptr;
    }

    void PlayerService::PutDownLocalItemCarry(bool immediate, bool refused) {
        SDK::wh::entitymodule::C_ActorActionCarryItem *action = ItemCarryActionFor(KCDC::Common::LocalActorEntityId());
        if (action == nullptr) {
            return;
        }
        if (refused) {
            if (CarryItemRecord *record = FindCarryItemRecord(action->GetItemId().value)) {
                record->refused = true;
            }
        }

        // The put-down only plays from the arms; a carry still being picked
        // up, or one asked to end now, is finished outright and Exit drops
        // the item where it is.
        if (immediate || action->GetPhase() != SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::Carrying) {
            action->Finish();
            return;
        }
        action->BeginPutDownAnimation();
    }

    void PlayerService::ReleaseItemCarryBy(SDK::EntityId carrierEntityId) {
        SDK::wh::entitymodule::C_ActorActionCarryItem *action = ItemCarryActionFor(carrierEntityId);
        if (action == nullptr) {
            return;
        }
        Framework::Logging::GetLogger(kLogger)->info("Entity {} lets go of item {} at once", carrierEntityId, action->GetItemId().value);

        // The queue runs Exit there and then when it can end the action
        // immediately, which is what drops the item off the body that is
        // about to go.
        action->Finish();
        if (ItemCarryActionFor(carrierEntityId) == action) {
            Framework::Logging::GetLogger(kLogger)->warn("The queue of entity {} deferred ending its item carry", carrierEntityId);
        }
    }

    void PlayerService::SweepCarryItems() {
        if (_carryItems.empty()) {
            return;
        }

        auto *world              = KCDC::Features::GroundItem::WorldInventory();
        const MafiaNet::Time now = MafiaNet::GetTime();
        std::erase_if(_carryItems, [world, now](const CarryItemRecord &record) {
            if (record.carried || now < record.removeAt) {
                return false;
            }

            // Nothing took it over: the world made it and still owns it, so it
            // is the world that removes it, pickable and all, as it does a
            // ground item's. One something else owns now stays theirs.
            SDK::wh::entitymodule::C_Item *item = world != nullptr ? KCDC::Features::GroundItem::FindItem(record.itemWuid) : nullptr;
            if (item != nullptr && item->GetOwner() == world) {
                world->RemoveItem(item);
            }
            return true;
        });
    }

    void PlayerService::ForgetItemCarries() {
        // The items went with the level that held them.
        _itemCarryActions.clear();
        _carryItems.clear();
        _enteringItemCarry = nullptr;
    }

    PlayerService::LocalAttackCommitCapture::LocalAttackCommitCapture(SDK::wh::combatmodule::C_CombatActor *combatActor, std::int32_t combatZone): _service(KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetPlayerService() : nullptr) {
        if (_service == nullptr) {
            return;
        }
        _previousFighter                       = _service->_localAttackCommitFighter;
        _previousCombatZone                    = _service->_localAttackCommitCombatZone;
        _service->_localAttackCommitFighter    = combatActor;
        _service->_localAttackCommitCombatZone = combatZone;
    }

    PlayerService::LocalAttackCommitCapture::~LocalAttackCommitCapture() {
        if (_service == nullptr) {
            return;
        }
        _service->_localAttackCommitFighter    = _previousFighter;
        _service->_localAttackCommitCombatZone = _previousCombatZone;
    }

    bool PlayerService::IsCapturingLocalAttackCommit(const SDK::wh::combatmodule::C_CombatActor *combatActor) const {
        return combatActor != nullptr && combatActor == _localAttackCommitFighter;
    }

    // --- Equipment --------------------------------------------------------

    void PlayerService::NotifyEquipmentChanged() {
        _equipmentChanged.store(true, std::memory_order_relaxed);
    }

    bool PlayerService::ConsumeEquipmentChanged() {
        return _equipmentChanged.exchange(false, std::memory_order_relaxed);
    }

    // --- Held items and speech --------------------------------------------

    bool PlayerService::PublishLocalHandItemClass(std::uint32_t hand, const Shared::RPC::ItemClassId &itemClass) {
        auto *owner = KCDC::Features::Player::Human::FindLocal();
        if (owner == nullptr) {
            return false;
        }

        owner->handItemClass[hand] = itemClass;
        return true;
    }

    void PlayerService::TalkFromChat(std::uint64_t speakerNetworkId, const std::string &text) {
        if (speakerNetworkId == 0 || text.empty()) {
            return;
        }

        if (auto *speaker = KCDC::Common::FindReplicaById<Human>(speakerNetworkId)) {
            speaker->Talk(text);
        }
    }

    void PlayerService::RequestLocalBody(Shared::Game::AppearanceGender gender) {
        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        if (actor == nullptr) {
            return;
        }

        // Already standing on it: nothing to rebuild, and an earlier request
        // for the other skeleton is withdrawn with it.
        const char *model = Human::BodyProperties(gender)[0].value;
        if (_stricmp(actor->GetCurrentModel(), model) == 0) {
            _bodySwapPending = false;
            return;
        }

        _bodySwapPending  = true;
        _bodySwapEntityId = actor->GetEntityId();
        _bodySwapGender   = gender;
    }

    void PlayerService::OnAnimationsSynced() {
        if (!_bodySwapPending) {
            return;
        }

        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        if (actor == nullptr || actor->GetEntityId() != _bodySwapEntityId) {
            // The body the request was about went with a level change; the
            // new one is appearance-applied afresh and asks again.
            _bodySwapPending = false;
            return;
        }

        // Rebuilding ends every action the body is in, which Revive only ever
        // does to a dead one: a ride would lose its rider action without Exit,
        // and a held item has no record anything re-attaches it from. So the
        // rebuild waits for the body to be Idle -- the state
        // C_RiderExpansion::CanMount asks for too -- out of combat and with
        // empty hands.
        const SDK::wh::entitymodule::C_ActorModel *state           = actor->GetActorModel();
        const SDK::wh::entitymodule::C_ItemAttachmentManager *held = actor->GetItemAttachmentManager();
        if (state == nullptr || !state->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Idle) || state->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Combat) || (held != nullptr && !held->InvokeAreHandsEmpty())) {
            return;
        }
        _bodySwapPending = false;

        // The Properties the rebuild records its model from and the clothing
        // config it reads live, restated for the other gender.
        SDK::IEntity *entity           = actor->GetEntity();
        auto *proxy                    = entity != nullptr ? static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script))) : nullptr;
        SDK::IScriptTable *scriptTable = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        auto properties                = scriptTable != nullptr ? scriptTable->GetTable("Properties") : SDK::ScriptTablePtr {};
        if (!properties) {
            Framework::Logging::GetLogger(kLogger)->error("The local body has no script Properties; it stays on {}", actor->GetCurrentModel());
            return;
        }
        const std::array<Human::BodyProperty, 3> body = Human::BodyProperties(_bodySwapGender);
        for (const Human::BodyProperty &property : body) {
            properties->SetValue(property.key, property.value);
        }

        // C_Actor::Revive's order: reset the animated character (which resets
        // the action controller, ending every clip while the old character
        // still exists), record and load the model and rebind Mannequin
        // (SetupActorModel, slot 247), then queue the pose actions again.
        SDK::CAnimatedCharacter *animated = actor->GetAnimatedCharacterRuntime();
        if (animated != nullptr) {
            animated->ResetState();
        }
        actor->SetupActorModel(body[0].value);
        // The reset zeroed the inertia Physicalize gave the character, as
        // after a revive; hand it back the same way.
        actor->RestoreAnimatedCharacterInertia();

        // The one step Revive never needs: the look and aim contexts cache the
        // character's pose blenders when they are initialised, and a clip
        // entering on either called straight into the freed one.
        SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
        if (controller != nullptr) {
            for (const char *name : {"ProceduralContextLook", "ProceduralContextAim"}) {
                if (SDK::IProceduralContext *context = controller->FindProceduralContext(name)) {
                    context->Initialise(*entity, *controller);
                }
            }
        }
        actor->SetupProceduralAnimationContexts(true);
        Human::ApplyGenderTag(actor, _bodySwapGender);

        // The wake's own tail: the clothing and the stored weapons were built
        // onto the character just released.
        actor->RefreshClothingSupport();
        actor->RebuildClothing();
        if (SDK::wh::entitymodule::C_ActorEquipmentHandler *equipment = actor->GetEquipmentHandler()) {
            equipment->ReattachToBody();
        }

        // The game's `IsFemale(user)` gates -- Horse:IsUsable "Women can't
        // use horses", AlchemyTable, Grindstone, the corpse grab and the
        // pickpocket prompt -- exist because the female database has none of
        // those animations. This body plays them from the male fallback, so
        // the rule is lifted where it is written. IsFemale is only ever
        // asked about the local player, and soul:GetGender stays truthful.
        if (_bodySwapGender == Shared::Game::AppearanceGender::Female) {
            static const char kLiftFemaleGates[] = "IsFemale = function() return false end";
            SDK::IScriptSystem *scripts          = SDK::IScriptSystem::GetInstance();
            if (scripts == nullptr || !scripts->ExecuteBuffer(kLiftFemaleGates, sizeof(kLiftFemaleGates) - 1, "IsFemale", nullptr)) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not lift the game's women-only interaction gates");
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("The local body now stands on {}", actor->GetCurrentModel());
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        using RequestJump_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorStateExpansion *);

        RequestJump_t g_requestJumpOriginal = nullptr;

        /**
         * Every jump the engine accepts, for every body in the world.
         *
         * The whole edge: jump input becomes a flag on the frame's movement
         * request and `C_ActorStateMovement::GroundMovement` turns it into
         * this call. The gate is answered before the action is built, so a
         * true return is a jump that is happening, not one that was asked for.
         *
         * A remote player's body jumps through its own input and arrives back
         * here too; only the local player's body is counted.
         */
        bool __fastcall RequestJump_Hook(SDK::wh::entitymodule::C_ActorStateExpansion *expansion) {
            const bool accepted = g_requestJumpOriginal(expansion);

            SDK::wh::entitymodule::C_ActionActor *actionActor = expansion != nullptr ? expansion->GetActionActor() : nullptr;
            SDK::wh::entitymodule::C_Actor *actor             = actionActor != nullptr ? actionActor->GetActor() : nullptr;
            if (!accepted || actor == nullptr || KCDC::Core::gApplication == nullptr) {
                return accepted;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyJumpStarted(actor->GetEntityId());
            return accepted;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initJumpHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorStateExpansion__RequestJump), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RequestJump_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_requestJumpOriginal));
    },
    "Jump");

namespace KCDC::Features::Player {
    namespace {
        using OnSlowdown_t = bool(__fastcall *)(SDK::wh::entitymodule::C_RiderPlayerInput *);

        OnSlowdown_t g_onSlowdownOriginal = nullptr;

        /**
         * Every `horse_slowdown` press a rider input takes.
         *
         * The handler is the only place the press exists: it latches the
         * brake, and `BuildHorseMovementRequest` consumes the latch within the
         * same frame. The action is press-only, so one call is one press.
         * Its arguments are never read, so only the receiver is passed on.
         */
        bool __fastcall OnSlowdown_Hook(SDK::wh::entitymodule::C_RiderPlayerInput *riderInput) {
            const bool handled = g_onSlowdownOriginal(riderInput);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifySlowdownPressed(riderInput);
            }
            return handled;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initRiderHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerInput__OnSlowdown), reinterpret_cast<LPVOID>(&KCDC::Features::Player::OnSlowdown_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_onSlowdownOriginal));
    },
    "RiderSlowdown");

namespace KCDC::Features::Player {
    namespace {
        using LadderEnter_t  = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionLadder *, void *);
        using LadderExit_t   = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionLadder *);
        using TryLedgeGrab_t = bool(__fastcall *)(void *, SDK::wh::entitymodule::C_Actor *, void *, std::uint16_t, SDK::wh::entitymodule::S_LedgeGrabInput *);

        using LadderTraffic_t = std::uint64_t(__fastcall *)(void *, SDK::EntityId, std::uint32_t, std::uint64_t, std::int32_t, float *, float, bool);

        LadderEnter_t g_ladderEnterOriginal     = nullptr;
        LadderExit_t g_ladderExitOriginal       = nullptr;
        LadderTraffic_t g_ladderTrafficOriginal = nullptr;
        TryLedgeGrab_t g_tryLedgeGrabOriginal = nullptr;

        /**
         * Every climb that starts, for every body in the world.
         *
         * Hooked here rather than at the ways in -- Lua's GrabOnLadder, the
         * AI's C_MovementBlockUseLadder, our own reproduction -- because all
         * of them end here. A false return is the action refusing to start,
         * usually because the ladder entity is not streamed in.
         */
        bool __fastcall LadderEnter_Hook(SDK::wh::entitymodule::C_ActorActionLadder *action, void *previousAction) {
            const bool entered = g_ladderEnterOriginal(action, previousAction);
            if (entered && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyLadderEntered(action);
            }
            return entered;
        }

        /** The other end of the same lifecycle, however the climb ended. */
        void __fastcall LadderExit_Hook(SDK::wh::entitymodule::C_ActorActionLadder *action) {
            g_ladderExitOriginal(action);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyLadderExited(action);
            }
        }

        /**
         * The ladder's traffic check (C_LadderTraffic::RegulateClimb,
         * 0x1811D4C54), which every climb step asks before it moves its
         * climber: a player-controlled climber whose rungs run into another
         * climber's on the same ladder has its input zeroed, whichever way it
         * climbs. The step has already eased its climb speed from the input
         * it was given, so a climb the player keeps holding kept its speed and
         * went through the other body. The local player's climb towards
         * another body is stopped with its input; one away from it, which the
         * check refuses as well once the two overlap, is given back. A remote
         * body counts as player-controlled here as well, but climbs as its
         * owner does, who was held on their own machine: its input is given
         * back.
         */
        std::uint64_t __fastcall LadderTraffic_Hook(void *traffic, SDK::EntityId climber, std::uint32_t rung, std::uint64_t unknown, std::int32_t rungCount, float *input, float rungFraction, bool playerControlled) {
            const float asked    = *input;
            const auto regulated = g_ladderTrafficOriginal(traffic, climber, rung, unknown, rungCount, input, rungFraction, playerControlled);
            if (asked == 0.0F || *input != 0.0F || KCDC::Core::gApplication == nullptr) {
                return regulated;
            }
            if (NativePlayerModule::IsRemoteEntity(climber)) {
                *input = asked;
                return regulated;
            }
            SDK::wh::entitymodule::C_Actor *local = KCDC::Common::LocalHumanActor();
            if (local == nullptr || local->GetEntityId() != climber) {
                return regulated;
            }
            PlayerService &players = KCDC::Core::gApplication->GetPlayerService();
            if (!players.IsClimberAhead(climber, asked)) {
                *input = asked;
            }
            else if (SDK::wh::entitymodule::C_ActorActionLadder *ladder = players.LadderActionFor(climber)) {
                ladder->StopClimb();
            }
            return regulated;
        }

        /**
         * Every vault the engine accepts, for every body in the world.
         *
         * The gates are answered inside before the action is posted, so a
         * true return is a vault that is happening -- the same edge the jump
         * hook reports. A receiver replays this very call on its copies, so
         * their vaults arrive here too; only the local player's is reported.
         *
         * The player's own vault arrives from C_PlayerInput::PreUpdate with
         * the jump and crouch request it was asked with, which travels with
         * it: the receiver's copy is player-controlled and is refused
         * without the same request.
         */
        bool __fastcall TryLedgeGrab_Hook(void *unused, SDK::wh::entitymodule::C_Actor *actor, void *actorAgain, std::uint16_t objectIndexFilter, SDK::wh::entitymodule::S_LedgeGrabInput *input) {
            const bool vaulting = g_tryLedgeGrabOriginal(unused, actor, actorAgain, objectIndexFilter, input);
            if (!vaulting || actor == nullptr || input == nullptr || KCDC::Core::gApplication == nullptr) {
                return vaulting;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyLedgeGrabStarted(actor->GetEntityId(), *input);
            return vaulting;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initTraversalHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionLadder__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LadderEnter_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_ladderEnterOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionLadder__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LadderExit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_ladderExitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_LadderTraffic__RegulateClimb), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LadderTraffic_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_ladderTrafficOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.TryLedgeGrab), reinterpret_cast<LPVOID>(&KCDC::Features::Player::TryLedgeGrab_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_tryLedgeGrabOriginal));
    },
    "Traversal");

// The engine's own edges for the local body's pace and its movement
// permissions. Nothing here is polled: the pace byte at C_PlayerInput+0x7C is
// written on the same edges the engine writes it on, never read back and
// corrected, because OnSprint rewrites it on every frame the key is held and
// a tick that disagreed would alternate with it at frame rate.
namespace KCDC::Features::Player {
    namespace {
        PlayerService *Players() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetPlayerService() : nullptr;
        }

        using Construct_t = SDK::wh::entitymodule::C_PlayerInput *(__fastcall *)(SDK::wh::entitymodule::C_PlayerInput *, void *);

        Construct_t g_constructOriginal = nullptr;

        /**
         * The constructor's store of 1 at +0x7C is the whole reason the game
         * starts in jog, so a default pace belongs immediately after it.
         *
         * Pace only: this runs from `C_Player::C_Player`, where
         * `SetMovementRestriction` would dereference the game object at
         * `C_Actor+0x38` that `Init` has not written yet.
         */
        SDK::wh::entitymodule::C_PlayerInput *__fastcall Construct_Hook(SDK::wh::entitymodule::C_PlayerInput *input, void *player) {
            SDK::wh::entitymodule::C_PlayerInput *built = g_constructOriginal(input, player);
            if (auto *players = Players(); players != nullptr && !KCDC::Features::Player::NativePlayerModule::IsRemoteExecution() && !KCDC::Features::Player::NativePlayerModule::IsRemote(built->GetPlayer())) {
                players->ApplyWalkDefault(built);
            }
            return built;
        }

        using OnSprint_t = bool(__fastcall *)(SDK::wh::entitymodule::C_PlayerInput *, unsigned int, const SDK::CCryName &, int, float);

        OnSprint_t g_onSprintOriginal = nullptr;

        /**
         * Forces jog whenever the sprint-held byte is set, and the action is
         * `onHold="1"` with a zero repeat delay -- so it lands on every frame
         * the key is down, including the frames where the sprint itself is
         * refused by the restart cooldown (0x180830108) or by stamina.
         *
         * `StartSprint` has already run by this point in hold mode, so the
         * sprint bit says which of those this was.
         */
        bool __fastcall OnSprint_Hook(SDK::wh::entitymodule::C_PlayerInput *input, unsigned int entityId, const SDK::CCryName &action, int activationMode, float value) {
            const bool wasWalking = input != nullptr && !input->IsRunMode();
            const bool handled    = g_onSprintOriginal(input, entityId, action, activationMode, value);

            if (auto *players = Players(); players != nullptr && !KCDC::Features::Player::NativePlayerModule::IsRemoteExecution() && !KCDC::Features::Player::NativePlayerModule::IsRemote(input->GetPlayer())) {
                players->NotifySprintForcedJog(input, wasWalking);
            }

            return handled;
        }

        using StopSprint_t = void(__fastcall *)(SDK::wh::entitymodule::C_PlayerInput *);

        StopSprint_t g_stopSprintOriginal = nullptr;

        /** Every way out of a sprint: key release, movement stopping, stamina. */
        void __fastcall StopSprint_Hook(SDK::wh::entitymodule::C_PlayerInput *input) {
            g_stopSprintOriginal(input);

            if (auto *players = Players(); players != nullptr && !KCDC::Features::Player::NativePlayerModule::IsRemoteExecution() && !KCDC::Features::Player::NativePlayerModule::IsRemote(input->GetPlayer())) {
                players->NotifySprintEnded(input);
            }
        }

        using SetMovementRestriction_t = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, bool, bool);

        SetMovementRestriction_t g_setMovementRestrictionOriginal = nullptr;

        /**
         * The only writer of the run and sprint permissions, so a rule that
         * holds them is kept by answering here. `C_ActorActionCarryItem::
         * SetCarryState` is the game's own caller; any other body passes
         * through untouched.
         */
        void __fastcall SetMovementRestriction_Hook(SDK::wh::entitymodule::C_Actor *actor, bool allowRun, bool allowSprint) {
            if (auto *players = Players()) {
                players->InterceptMovementRestriction(actor, allowRun, allowSprint);
            }

            g_setMovementRestrictionOriginal(actor, allowRun, allowSprint);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initMovementModeHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__Construct), reinterpret_cast<LPVOID>(&KCDC::Features::Player::Construct_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_constructOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__OnSprint), reinterpret_cast<LPVOID>(&KCDC::Features::Player::OnSprint_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_onSprintOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__StopSprint), reinterpret_cast<LPVOID>(&KCDC::Features::Player::StopSprint_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_stopSprintOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__SetMovementRestriction), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetMovementRestriction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setMovementRestrictionOriginal));
    },
    "Movement mode");

namespace KCDC::Features::Player {
    namespace {
        using CarryEnter_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryCorpse *, void *);
        using CarryExit_t  = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryCorpse *);

        CarryEnter_t g_carryEnterOriginal = nullptr;
        CarryExit_t g_carryExitOriginal   = nullptr;

        using CarryItemEnter_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryItem *, void *);
        using CarryItemExit_t  = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryItem *);

        CarryItemEnter_t g_carryItemEnterOriginal = nullptr;
        CarryItemExit_t g_carryItemExitOriginal   = nullptr;

        using CarryItemBeginPutDown_t = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryItem *);

        CarryItemBeginPutDown_t g_carryItemBeginPutDownOriginal = nullptr;

        using CarryItemBeginPickup_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCarryItem *);

        CarryItemBeginPickup_t g_carryItemBeginPickupOriginal = nullptr;

        /**
         * Every carry that starts, for every body in the world.
         *
         * Hooked here rather than at the ways in -- Lua's `RequestGrabCorpse`,
         * the AI's `PickUpBody`, a quest's forced carry, our own replay --
         * because all of them end here. A false return is the carrier's
         * `C_ActorCarryCorpse` refusing, as it does for the carrier itself.
         *
         * A body that no longer resolves is refused here first. The instant
         * grab refuses it too (`AttachCarriedInstantly`, 0x18285FFB8), but the
         * animated one (`BeginPickup`, 0x18285F580) binds whatever it looked
         * up without checking, and a replayed grab is queued a frame before
         * it enters -- a frame in which its body can be despawned.
         */
        bool __fastcall CarryEnter_Hook(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action, void *previousAction) {
            auto *actorSystem = SDK::IActorSystem::GetInstance();
            if (actorSystem == nullptr || actorSystem->GetActor(action->GetTargetEntityId()) == nullptr) {
                return false;
            }

            const bool entered = g_carryEnterOriginal(action, previousAction);
            if (entered && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyCarryEntered(action);
            }
            return entered;
        }

        /** The other end of the same lifecycle, however the carry ended. */
        void __fastcall CarryExit_Hook(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action) {
            g_carryExitOriginal(action);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyCarryExited(action);
            }
        }

        /**
         * Every item carry that starts, for every body in the world: a pile's
         * `PickUp`, a carryable item's, and `StartItemCarry`'s own -- all of
         * them post this action. A false return is the action refusing,
         * which it does without ever reaching Exit.
         */
        bool __fastcall CarryItemEnter_Hook(SDK::wh::entitymodule::C_ActorActionCarryItem *action, void *previousAction) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().PrepareItemCarryEnter(action);
            }
            const bool entered = g_carryItemEnterOriginal(action, previousAction);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyItemCarryEntered(action, entered);
            }
            return entered;
        }

        /** The other end of the same lifecycle: put down, deposited, interrupted or finished. */
        void __fastcall CarryItemExit_Hook(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
            {
                // Exit clears the put_item callback and switches the carry
                // action maps on this machine's one input system; a remote
                // carrier's carry leaves them to the local player.
                const KCDC::Features::Player::NativePlayerModule::RemoteActionScope scope(KCDC::Features::Player::NativePlayerModule::IsRemote(action->GetActionActor()->GetActor()));
                g_carryItemExitOriginal(action);
            }
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyItemCarryExited(action);
            }
        }

        /**
         * Every put-down clip that starts: the game's `put_item` callback,
         * our put-down order and a copy's replay all begin here. It enters
         * phase 4 only when the carrier has a put-down fragment to play.
         */
        void __fastcall CarryItemBeginPutDown_Hook(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
            g_carryItemBeginPutDownOriginal(action);
            if (KCDC::Core::gApplication != nullptr && action->GetPhase() == SDK::wh::entitymodule::C_ActorActionCarryItem::E_Phase::PuttingDown) {
                KCDC::Core::gApplication->GetPlayerService().NotifyItemPutDownStarted(action);
            }
        }

        /**
         * Every item pick-up clip that starts, for every body.
         *
         * The game aims the pick-up two ways (0x181083914). An item whose
         * holder is an entity -- a pile -- leaves the item where it is and
         * hands the fragment that entity's pose as `TargetPos`, which the
         * PositionAdjust procedural clips bring the carrier to. An item loose
         * in the world instead has the item moved to where the clip holds it
         * in front of the carrier, and every prop is loose in the world: it
         * jumped before the clip began.
         *
         * Such an item is put back, and the fragment is aimed the pile's way:
         * at the carrier pose that holds the item where the game would have
         * moved it -- measured from that very move -- so the carrier is
         * brought to the item rather than the item to the carrier.
         */
        bool __fastcall CarryItemBeginPickup_Hook(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
            SDK::wh::entitymodule::C_Item *item = KCDC::Features::GroundItem::FindItem(action->GetItemId().value);
            auto *pickable                      = item != nullptr ? item->GetPickableItem() : nullptr;
            SDK::IEntity *itemEntity            = pickable != nullptr ? pickable->GetEntity() : nullptr;
            if (itemEntity == nullptr) {
                return g_carryItemBeginPickupOriginal(action);
            }
            const SDK::Matrix34 lying     = itemEntity->GetWorldTM();
            const glm::vec3 lyingPosition = KCDC::Common::ToGlm(itemEntity->GetWorldPos());

            const bool begun = g_carryItemBeginPickupOriginal(action);
            if (!begun) {
                return false;
            }
            const glm::vec3 heldPosition = KCDC::Common::ToGlm(itemEntity->GetWorldPos());
            if (glm::distance(lyingPosition, heldPosition) < 0.001F) {
                return true;
            }

            SDK::IEntity *carrierEntity     = action->GetActionActor()->GetActor()->GetEntity();
            const glm::vec3 carrierPosition = KCDC::Common::ToGlm(carrierEntity->GetWorldPos());
            const glm::quat carrierRotation = glm::normalize(KCDC::Common::ToGlm(carrierEntity->GetWorldRotation()));
            SDK::QuatT target {};
            target.q = KCDC::Common::ToNative(carrierRotation);
            target.t = KCDC::Common::ToNative(carrierPosition + (lyingPosition - heldPosition));
            itemEntity->SetWorldTM(lying);
            action->GetFragmentAction()->SetParam("TargetPos", target);
            return true;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initCarryHooks(
    []() {
        const auto enter = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryCorpse__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryEnter_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryEnterOriginal));
        const auto exit  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryCorpse__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryExit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryExitOriginal));
        const auto itemEnter = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryItemEnter_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryItemEnterOriginal));
        const auto itemExit  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryItemExit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryItemExitOriginal));
        const auto itemPutDown = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__BeginPutDownAnimation), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryItemBeginPutDown_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryItemBeginPutDownOriginal));
        const auto itemPickup  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__BeginItemPickupAnimation), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CarryItemBeginPickup_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_carryItemBeginPickupOriginal));
        if (enter != MH_OK || exit != MH_OK || itemEnter != MH_OK || itemExit != MH_OK || itemPutDown != MH_OK || itemPickup != MH_OK) {
            throw std::runtime_error("Could not install the carry hooks");
        }
    },
    "PlayerCarry");

#if defined(KCDC_AGENT_TESTING)
#endif






/**
 * Women's bodies, grounded in the files the game already ships.
 *
 * A woman is `female.cdf` under the same Mannequin set as a man
 * (`kcd_male_*`), because the game only ever authored riding, weapon draws,
 * combat and deaths for men: `wh_female_database.adb` has none of them. What
 * she does have she keeps, through the engine's own mechanisms, applied to
 * the game's files as they are read:
 *
 * - the man's Mannequin files gain everything only hers declare, and his
 *   database gains her fragments behind a `female` tag
 *   (`FemaleBody::ExtendMannequinFile`, player_female_body.cpp), so a tagged
 *   body plays her walk, her idles and every other fragment she has, and his
 *   only where she has none. Every body on her skeleton -- the game's own
 *   women included -- is built on that set, tagged
 *   (`CAnimatedCharacter::InitializeMannequin`);
 * - `female.chrparams` gains the engine's own `$Include` of
 *   `male.chrparams`, so those male fragments' clips exist on her skeleton.
 *   Her list loads first and `LoadFileCAF` keeps the first clip of a name.
 *
 * Those male clips name her hand joints right but not their frames, so her
 * hand joints are put on the man's frames as everything that carries them
 * loads -- her skeleton, her databases, her aim and look poses -- and what
 * was authored against her own frames is converted with them
 * (`FemaleBody::AdoptMaleHandFrames*`, player_female_body.cpp). Nothing of it
 * runs while a body animates.
 */
namespace KCDC::Features::Player {
    namespace {
        using LoadXmlFromFile_t           = SDK::XmlNodeRef *(__fastcall *)(SDK::CXmlUtils *, SDK::XmlNodeRef *, const char *, bool, bool, bool, bool);
        using InitializeMannequin_t       = bool(__fastcall *)(SDK::CAnimatedCharacter *, void *);
        using GetControllerByJointCRC32_t = SDK::IController *(__fastcall *)(const SDK::GlobalAnimationHeaderCAF *, std::uint32_t);
        using SyncAllAnimations_t         = void(__fastcall *)(SDK::CharacterManager *);
        using InitializeBones_t           = bool(__fastcall *)(SDK::CDefaultSkeleton *, SDK::CSkinningInfo *, const char *);
        using LoadNewSKEL_t               = bool(__fastcall *)(SDK::CDefaultSkeleton *, const char *, std::uint32_t);
        using ReadController905_t         = bool(__fastcall *)(SDK::CInternalDatabaseInfo *, SDK::IChunkFile::ChunkDesc *, bool);
        using InitControllerLookup_t      = std::int64_t(__fastcall *)(SDK::GlobalAnimationHeaderCAF *, std::int32_t);
        using InternalDatabaseInfoDtor_t  = void(__fastcall *)(SDK::CInternalDatabaseInfo *);
        // The engine's _smart_ptr return has an explicit hidden buffer in
        // RDX, after this in RCX. Opaque arguments are only forwarded.
        using CreateRenderMesh_t = void *(__fastcall *)(void *, void *, SDK::CMesh *, const char *, int, bool, bool);

        LoadXmlFromFile_t g_loadXmlFromFileOriginal                     = nullptr;
        InitializeMannequin_t g_initializeMannequinOriginal             = nullptr;
        GetControllerByJointCRC32_t g_getControllerByJointCRC32Original = nullptr;
        SyncAllAnimations_t g_syncAllAnimationsOriginal                 = nullptr;
        InitializeBones_t g_initializeBonesOriginal                     = nullptr;
        LoadNewSKEL_t g_loadNewSKELOriginal                             = nullptr;
        ReadController905_t g_readController905Original                 = nullptr;
        InitControllerLookup_t g_initControllerLookupOriginal           = nullptr;
        InternalDatabaseInfoDtor_t g_internalDatabaseInfoDtorOriginal   = nullptr;
        CreateRenderMesh_t g_createRenderMeshOriginal                   = nullptr;

        bool StartsWithPathNoCase(const char *path, const char *prefix) {
            for (; *prefix != '\0'; ++path, ++prefix) {
                const char value = *path == '\\' ? '/' : *path;
                if (std::tolower(static_cast<unsigned char>(value)) != *prefix) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Many NPC heads lack FirstPersonView vertices. The book camera sits
         * inside their eyes and mouth because the shader has nothing to
         * hide. Supply group zero to sections with no authored mask; keep
         * authored sections, including Henry's visible neck vertices. This
         * also repairs young Henry's unmarked lashes and eye overlays even
         * though his skin and eyes already have first-person membership.
         * NPC hair and caps, and every female hair and cap, lack the mask
         * too: a player wearing one sees it from inside the head when
         * looking up (HairCapBrabant_m01 marks 0 of 1118 vertices).
         *
         * WHHidingGroups.cfi inverts alpha before testing the active mask,
         * so clearing alpha bit zero assigns a vertex to FirstPersonView.
         * Shadow passes explicitly ignore that group; other camera views
         * leave it inactive. RGB and all other group bits stay unchanged.
         * Runs before upload for both synchronous loading and streamed LODs.
         */
        void RepairHeadFirstPersonMask(SDK::CMesh &mesh, const char *path) {
            constexpr const char *kPaths[] = {
                "objects/characters/humans/male/head/",
                "objects/characters/humans/female/head/",
                "objects/characters/humans/male/hair/",
                "objects/characters/humans/female/hair/",
                "objects/characters/humans/male/clothing/head/cap/",
                "objects/characters/humans/female/clothing/f_head/f_cap/",
            };
            if (path == nullptr || std::none_of(std::begin(kPaths), std::end(kPaths), [path](const char *prefix) {
                    return StartsWithPathNoCase(path, prefix);
                })) {
                return;
            }
            const SDK::SMeshColor *colors = mesh.GetColors();
            const int count               = colors != nullptr ? mesh.GetColorCount() : mesh.GetVertexCount();
            const int indexCount          = mesh.GetIndexCount();
            if (count <= 0 || mesh.GetIndices() == nullptr || indexCount <= 0) {
                return;
            }
            // SetMesh's colour copier (0x1805E0DEC) supplies RGBA white
            // when the stream is absent. Preserve that default for skins
            // such as m_head_050's beard_20, which has no colour chunk.
            std::vector<SDK::SMeshColor> defaultColors;
            if (colors == nullptr) {
                defaultColors.assign(count, SDK::SMeshColor(255, 255, 255, 255));
                colors = defaultColors.data();
            }
            const auto &subsets = mesh.GetSubsets();
            const auto repair   = KCDC::Common::FindMissingFirstPersonVertices({colors, static_cast<std::size_t>(count)}, {mesh.GetIndices(), static_cast<std::size_t>(indexCount)}, {subsets.m_pData, subsets.Size()});
            if (repair.empty()) {
                return;
            }
            // The CGF loader can lend CMesh its input buffer. Never mutate
            // borrowed storage or replace it with a mod-allocated buffer.
            SDK::SMeshColor *writableColors = mesh.GetWritableColors(count);
            if (writableColors == nullptr) {
                return;
            }
            if (!defaultColors.empty()) {
                std::copy(defaultColors.begin(), defaultColors.end(), writableColors);
            }
            for (int i = 0; i < count; ++i) {
                if (repair[i]) {
                    writableColors[i].MaskA(0xFE);
                }
            }
        }

        void *__fastcall CreateRenderMesh_Hook(void *modelMesh, void *result, SDK::CMesh *mesh, const char *path, int lod, bool createDeviceMesh, bool needsComputeSkinningBuffers) {
            RepairHeadFirstPersonMask(*mesh, path);
            return g_createRenderMeshOriginal(modelMesh, result, mesh, path, lod, createDeviceMesh, needsComputeSkinningBuffers);
        }

        bool EndsWithNoCase(const char *text, const char *suffix) {
            const std::size_t textLength   = std::strlen(text);
            const std::size_t suffixLength = std::strlen(suffix);
            if (textLength < suffixLength) {
                return false;
            }
            const char *tail = text + (textLength - suffixLength);
            for (std::size_t i = 0; i < suffixLength; ++i) {
                const char a = tail[i] == '\\' ? '/' : tail[i];
                if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(suffix[i]))) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Appends `<childTag key0=value0 key1=value1>` under `section` of a
         * clone of `document`, and hands the clone back through `document`.
         * Cloned because a document out of a pak can be the read-only node,
         * on which every write is silently dropped.
         */
        void AppendChild(SDK::XmlNodeRef &document, const char *section, const char *childTag, const char *key0, const char *value0, const char *key1, const char *value1) {
            SDK::XmlNodeRef edited = document->clone(true);
            if (!edited.IsValid()) {
                return;
            }
            SDK::XmlNodeRef parent = edited->findChild(section);
            if (!parent.IsValid()) {
                return;
            }
            SDK::XmlNodeRef child = parent->newChild(childTag);
            if (!child.IsValid()) {
                return;
            }
            child->setAttr(key0, value0);
            if (key1 != nullptr) {
                child->setAttr(key1, value1);
            }
            document = edited;
        }

        /** Every XML the engine reads by path; a few of the game's files are extended as they load. */
        SDK::XmlNodeRef *__fastcall LoadXmlFromFile_Hook(SDK::CXmlUtils *utils, SDK::XmlNodeRef *result, const char *file, bool flag0, bool flag1, bool useReadOnlyParser, bool flag3) {
            g_loadXmlFromFileOriginal(utils, result, file, flag0, flag1, useReadOnlyParser, flag3);
            if (file == nullptr || !result->IsValid()) {
                return result;
            }

#if defined(KCDC_AGENT_TESTING)
            KCDC::Core::Boot::AgentTesting::Get().ApplyProfilePreset(file, *result);
#endif

            if (EndsWithNoCase(file, "humans/female/skeleton/female.chrparams")) {
                // Spelled as male.cdf spells its model, so the include finds
                // the list the male skeleton already parsed.
                AppendChild(*result, "AnimationList", "Animation", "name", "$Include", "path", "objects/characters/humans/male/skeleton/male.chrparams");
            }
            else if (EndsWithNoCase(file, ".adb") || EndsWithNoCase(file, "mannequin/adb/kcd_male_tags.xml") || EndsWithNoCase(file, "mannequin/adb/kcd_male_fragmentids.xml") || EndsWithNoCase(file, "mannequin/adb/kcd_male_controllerdefs.xml")) {
                // The other game files it reads are parsed with this call's
                // own flags, past this hook.
                FemaleBody::ExtendMannequinFile(file, *result, [utils, flag0, flag1, useReadOnlyParser, flag3](const char *path) {
                    SDK::XmlNodeRef document;
                    g_loadXmlFromFileOriginal(utils, &document, path, flag0, flag1, useReadOnlyParser, flag3);
                    return document;
                });
            }
            else if ((*result)->isTag("ATLConfig")) {
                // A `Libs/GameAudio` control file, read by
                // `CATLXmlProcessor::ParseControlsData` before any trigger can
                // play; the siege engines' sounds and the dice throw's player
                // cues get their range here.
                KCDC::Features::Siege::SiegeModule::ExtendGameAudio(*result);
                KCDC::Features::Dice::DiceModule::ExtendGameAudio(*result);
            }
            else {
                // The reading minigame's exit, from the action map and hint
                // config; owned by the book feature.
                KCDC::Features::Book::BookService::ExtendGameConfig(file, *result);
            }
            return result;
        }

        /**
         * A body's Mannequin controller built from its class script, on
         * entity init, reset and extension reload. A body on her skeleton is
         * built on the man's set and marked a woman's.
         */
        bool __fastcall InitializeMannequin_Hook(SDK::CAnimatedCharacter *character, void *scratch) {
            const bool woman       = FemaleBody::AdoptHumanMannequin(character->GetEntity());
            const bool initialized = g_initializeMannequinOriginal(character, scratch);
            if (woman) {
                Human::MarkWomansController(character->GetActionController());
            }
            return initialized;
        }

        /**
         * A clip's track for one joint. A man's clip keys its root motion to
         * `Human_Male` and her skeleton asks for `Human_Female`, so a male
         * fragment on her would have no locator and no travel speed for a
         * blend space. Her own clips answer the first question. Runs on the
         * animation workers; reads nothing but its arguments.
         */
        SDK::IController *__fastcall GetControllerByJointCRC32_Hook(const SDK::GlobalAnimationHeaderCAF *header, std::uint32_t jointCrc) {
            SDK::IController *controller = g_getControllerByJointCRC32Original(header, jointCrc);
            if (controller == nullptr && jointCrc == SDK::kHumanFemaleRootJointCrc) {
                controller = g_getControllerByJointCRC32Original(header, SDK::kHumanMaleRootJointCrc);
            }
            return controller;
        }

        /**
         * A skeleton's bones, as the `.chr` loaded them, before anything is
         * derived from them: her hand joints go onto the man's frames here.
         */
        bool __fastcall InitializeBones_Hook(SDK::CDefaultSkeleton *skeleton, SDK::CSkinningInfo *skinning, const char *path) {
            if (FemaleBody::IsFemaleSkeleton(path)) {
                FemaleBody::AdoptMaleHandFrames(*skinning);
            }
            return g_initializeBonesOriginal(skeleton, skinning, path);
        }

        /**
         * A skeleton fully loaded, `.chrparams` included. Its aim and look IK
         * lists say which joint each entry of a baked pose is, and
         * `DirectionalBlends.img` was read before the first skeleton
         * (`CharacterManager::FetchModelSKEL`, 0x180AA8278).
         */
        bool __fastcall LoadNewSKEL_Hook(SDK::CDefaultSkeleton *skeleton, const char *path, std::uint32_t flags) {
            const bool loaded = g_loadNewSKELOriginal(skeleton, path, flags);
            if (loaded && FemaleBody::IsFemaleSkeleton(path)) {
                FemaleBody::AdoptMaleHandFramesInDirectionalPoses(*skeleton);
            }
            return loaded;
        }

        /** One database's clips built, on the loading or the streaming thread. */
        bool __fastcall ReadController905_Hook(SDK::CInternalDatabaseInfo *database, SDK::IChunkFile::ChunkDesc *chunk, bool streaming) {
            FemaleBody::BeginFemaleDatabase(*database, *chunk);
            const bool read = g_readController905Original(database, chunk, streaming);
            FemaleBody::EndFemaleDatabase();
            return read;
        }

        /**
         * A clip's controllers in place, the step before it counts as loaded
         * (ReadController905 writes its controller count right after).
         */
        std::int64_t __fastcall InitControllerLookup_Hook(SDK::GlobalAnimationHeaderCAF *clip, std::int32_t controllerCount) {
            FemaleBody::AdoptMaleHandFramesInClip(*clip, static_cast<std::uint32_t>(controllerCount));
            return g_initControllerLookupOriginal(clip, controllerCount);
        }

        /** A database going away, with the controllers its clips were given. */
        void __fastcall InternalDatabaseInfoDtor_Hook(SDK::CInternalDatabaseInfo *database) {
            FemaleBody::ReleaseFemaleDatabase(*database);
            g_internalDatabaseInfoDtorOriginal(database);
        }

        /** Every job joined: the one point a live character can be replaced. */
        void __fastcall SyncAllAnimations_Hook(SDK::CharacterManager *manager) {
            g_syncAllAnimationsOriginal(manager);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().OnAnimationsSynced();
            }
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initBodyHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CModelMesh__CreateRenderMesh), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CreateRenderMesh_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_createRenderMeshOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CXmlUtils__LoadXmlFromFile), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LoadXmlFromFile_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_loadXmlFromFileOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CAnimatedCharacter__InitializeMannequin), reinterpret_cast<LPVOID>(&KCDC::Features::Player::InitializeMannequin_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_initializeMannequinOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.GlobalAnimationHeaderCAF__GetControllerByJointCRC32), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetControllerByJointCRC32_Hook),
            reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_getControllerByJointCRC32Original));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CharacterManager__SyncAllAnimations), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SyncAllAnimations_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_syncAllAnimationsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.SkelLoader_Helpers__InitializeBones), reinterpret_cast<LPVOID>(&KCDC::Features::Player::InitializeBones_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_initializeBonesOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CDefaultSkeleton__LoadNewSKEL), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LoadNewSKEL_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_loadNewSKELOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CInternalDatabaseInfo__ReadController905), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ReadController905_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_readController905Original));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.GlobalAnimationHeaderCAF__InitControllerLookup), reinterpret_cast<LPVOID>(&KCDC::Features::Player::InitControllerLookup_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_initControllerLookupOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CInternalDatabaseInfo__Destructor), reinterpret_cast<LPVOID>(&KCDC::Features::Player::InternalDatabaseInfoDtor_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_internalDatabaseInfoDtorOriginal));
    },
    "PlayerBody");

namespace KCDC::Features::Player {
    namespace {
        /**
         * Takes fast travel off the map screen.
         *
         * The hooked routine runs when the player picks a POI, and it is the
         * only place the offer is raised: it puts the `start_fast_travel`
         * hint up and draws the route in Flash. Refusing the travel further
         * down removes neither -- it only greys the hint and labels it with
         * the reason -- so the detour forwards to the page's own teardown
         * instead, the one the map subscribes beside it for a selection that
         * travels nowhere. It takes the same two arguments and destroys the
         * POI GUID string that comes in by value.
         *
         * With no hint there is no action to press, so the cutscene, the
         * pending latch and C_FastTravel::StartTravel are unreachable from
         * the map. The travel gate itself is untouched.
         */
        void __fastcall PrepareFastTravelToPoi_Hook(void *mapPage, void *poiGuid) {
            hook::call<void>(SDK::gPatterns.ClearFastTravelPoi, mapPage, poiGuid);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initFastTravelHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.PrepareFastTravelToPoi), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PrepareFastTravelToPoi_Hook), nullptr);
    },
    "FastTravel");
