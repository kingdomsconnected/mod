#include <utils/safe_win32.h>

#include "features/player/player_rock_service.h"

#include "core/obfuscation.h"
#include "features/player/player.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/entitymodule/c_decoy.h"
#include "sdk/wh/entitymodule/c_entity_module.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <string_view>

namespace KCDC::Features::Player {
    PlayerRockService &PlayerRockService::Get() {
        static PlayerRockService service;
        return service;
    }

    void PlayerRockService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        // Client RPCs are dispatched from the game tick.
        net->RegisterRPC<KCDC::Shared::RPC::RockLaunched>([](const auto &message, MafiaNet::Packet *) { Get().Replay(message); });
    }

    void PlayerRockService::ProjectileLaunched(SDK::wh::entitymodule::CProjectile *projectile) {
        if (!SDK::wh::entitymodule::C_Decoy::Is(*projectile)) {
            return;
        }
        const SDK::EntityId localActorId = KCDC::Common::LocalActorEntityId();
        auto *net                        = Framework::CoreModules::GetNetworkPeer();
        const auto *local                = KCDC::Features::Player::Human::FindCapturedLocal();
        if (localActorId == 0 || projectile->GetOwnerEntityId() != localActorId || net == nullptr || local == nullptr) {
            return;
        }
        KCDC::Shared::RPC::RockLaunched report;
        const auto &position  = projectile->GetInitialPosition();
        const auto &direction = projectile->GetInitialDirection();
        const auto &velocity  = projectile->GetInitialVelocity();
        report.incarnation    = local->soul.incarnation;
        report.epoch          = local->stateEpoch;
        report.sequence       = _nextSequence;
        report.position       = {position.x, position.y, position.z};
        report.direction      = {direction.x, direction.y, direction.z};
        report.velocity       = {velocity.x, velocity.y, velocity.z};
        if (!report.IsSane()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Pebble release rejected: incarnation {}, direction ({}, {}, {}), velocity ({}, {}, {})", report.incarnation, direction.x, direction.y, direction.z, velocity.x, velocity.y, velocity.z);
            return;
        }
        ++_nextSequence;
        net->BroadcastRPC(report);
    }

    void PlayerRockService::Replay(const KCDC::Shared::RPC::RockLaunched &message) {
        auto *human    = message.IsSane() ? KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(message.throwerNetworkId) : nullptr;
        auto *actor    = human != nullptr && !human->IsOwner() && human->soul.incarnation == message.incarnation && human->stateEpoch == message.epoch ? human->ResolveActor() : nullptr;
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *module   = SDK::wh::entitymodule::C_EntityModule::GetInstance();
        auto *weapons  = module != nullptr ? module->GetWeaponSystem() : nullptr;
        if (actor == nullptr || !actor->IsSettled() || entities == nullptr || weapons == nullptr) {
            return;
        }
        auto *entityClass = entities->GetClassRegistry()->FindClass(std::string_view(KCDC_HIDE("Decoy")).data());
        auto *projectile  = entityClass != nullptr ? weapons->SpawnAmmo(entityClass) : nullptr;
        if (projectile == nullptr) {
            return;
        }
        // The pebble left the thrower's hand on their machine.
        human->ReleaseHeldDecoy();

        // The native distract-minigame spawn path, without attaching the
        // already released projectile to the copy's hand. An empty geometry
        // override makes SetParams load the native ammo geometry, and the
        // thrower's channel makes it raise the player-trail flag, which the
        // projectile's own Update turns into the trail (0x182AAE3CD).
        const SDK::wh::entitymodule::CProjectile::SProjectileDesc desc(actor->GetEntityId());
        projectile->SetParams(desc);
        projectile->Launch(SDK::Vec3 {message.position.x, message.position.y, message.position.z}, SDK::Vec3 {message.direction.x, message.direction.y, message.direction.z}, SDK::Vec3 {message.velocity.x, message.velocity.y, message.velocity.z}, 1.0F);
    }
} // namespace KCDC::Features::Player
