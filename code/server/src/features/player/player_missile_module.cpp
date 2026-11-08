#include "features/player/player_missile_module.h"

#include "core/peer_relay.h"
#include "core/server.h"
#include "features/npc/npc_inventory_module.h"
#include "features/player/player_combatant.h"

#include "shared/features/player/player_entity.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/missile_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>

namespace KCDC::Features::Player {
    void Missile::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::MissileLaunched>([this](const auto &message, MafiaNet::Packet *packet) { OnLaunched(message, packet); });
        net->RegisterRPC<Shared::RPC::MissileEnded>([this](const auto &message, MafiaNet::Packet *packet) { OnEnded(message, packet); });
        net->RegisterRPC<Shared::RPC::MissileImpact>([this](const auto &message, MafiaNet::Packet *packet) { OnImpact(message, packet); });
        net->RegisterRPC<Shared::RPC::MissileRetained>([this](const auto &message, MafiaNet::Packet *packet) { OnRetained(message, packet); });
    }

    void Missile::OnLaunched(const Shared::RPC::MissileLaunched &message, MafiaNet::Packet *packet) {
        if (packet == nullptr || !message.IsSane() || !Shared::Game::IsCompatibleMissileAmmunition(message.weapon, message.ammunition) || !Shared::Game::IsValidMissileLaunchProperties(message)) {
            return;
        }

        const auto guid   = MafiaNet::ToPeerGuid(packet->guid);
        const auto body   = Combatant::Sender(message.shooterNetworkId, guid);
        auto *shooter     = body.entity;

        if (shooter == nullptr || !body.Valid() || message.incarnation != body.Incarnation() || message.epoch != shooter->stateEpoch || !body.Ready() || !Shared::Game::IsWorldPosition(body.Position())) {
            return;
        }

        // An NPC shoots only for the bow attack it was ordered; a player never names a revision.
        if (body.npc != nullptr ? (message.npcRevision != body.npc->intentRevision || body.npc->intent.kind != Shared::Game::NpcIntentKind::Attack || !body.npc->intent.bow || Shared::Game::HasFlag(body.npc->flags, Shared::Game::NpcFlags::Frozen)) : message.npcRevision != 0) {
            return;
        }

        // The 12 m bound allows muzzle offset and transform latency.
        const auto delta = message.position - body.Position();

        if (glm::dot(delta, delta) > 144.0F) {
            return;
        }

        auto &player = _players[shooter->GetNetworkID()];

        // A delegated NPC's shots are numbered by the client simulating it,
        // and a new simulator counts from its own start. Its numbers are
        // fresh, but may still meet one of the last simulator's in flight.
        if (body.npc != nullptr && player.guid != guid) {
            player.lastSequence = 0;
        }

        player.guid = guid;

        if (message.sequence <= player.lastSequence || player.shots.contains(message.sequence)) {
            return;
        }

        player.lastSequence = message.sequence;

        const std::uint64_t now = MafiaNet::GetTime();

        if (player.refillAt != 0) {
            player.tokens = std::min(64.0, player.tokens + static_cast<double>(now - player.refillAt) * 0.008);
        }

        player.refillAt = now;

        if (player.tokens < 1.0 || player.shots.size() >= Shared::RPC::MissileLaunched::kMaxActivePerPlayer) {
            return;
        }

        player.tokens -= 1.0;

        auto relay = message;

        relay.shooterNetworkId = shooter->GetNetworkID();
        player.shots.emplace(message.sequence, Shared::Game::MissileShotRecord {relay, now, now + Shared::RPC::MissileLaunched::kLifetimeMs});
        // The peers that have the shooter's body draw its shot; the impact
        // on a target beyond them goes to that target itself.
        KCDC::RelayToStreamingPeers(relay, *shooter);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Missile launch accepted: shooter={} sequence={}", relay.shooterNetworkId, relay.sequence);
    }

    void Missile::OnEnded(const Shared::RPC::MissileEnded &message, MafiaNet::Packet *packet) {
        if (packet == nullptr || !message.IsSane()) {
            return;
        }

        const auto body = Combatant::Sender(message.shooterNetworkId, MafiaNet::ToPeerGuid(packet->guid));
        auto *shooter   = body.entity;

        if (shooter == nullptr) {
            return;
        }

        const auto found = _players.find(shooter->GetNetworkID());

        if (found == _players.end() || found->second.shots.erase(message.sequence) == 0) {
            return;
        }

        auto relay = message;

        relay.shooterNetworkId = shooter->GetNetworkID();
        KCDC::RelayToStreamingPeers(relay, *shooter);
    }

    void Missile::OnImpact(const Shared::RPC::MissileImpact &message, MafiaNet::Packet *packet) {
        if (packet == nullptr || !message.IsSane()) {
            return;
        }

        auto *net         = Framework::CoreModules::GetNetworkPeer();
        const auto body   = Combatant::Sender(message.launch.shooterNetworkId, MafiaNet::ToPeerGuid(packet->guid));
        const auto victim = Combatant::Find(message.targetNetworkId);
        auto *shooter     = body.entity;
        auto *target      = victim.entity;
        if (net == nullptr || !body || !victim || target == shooter || !victim.Ready()) {
            return;
        }
        const bool npcPair = body.npc != nullptr || victim.npc != nullptr;
        if (npcPair ? !MafiaNet::VirtualWorldsCanSee(target->GetVirtualWorld(), shooter->GetVirtualWorld()) : target->GetVirtualWorld() != shooter->GetVirtualWorld()) {
            return;
        }
        if (!body.Valid() || !victim.Valid() || message.soulAction.incarnation != body.Incarnation() || message.soulAction.epoch != shooter->stateEpoch || message.soulAction.targetIncarnation != victim.Incarnation() || message.soulAction.targetEpoch != target->stateEpoch) {
            return;
        }
        if (body.npc != nullptr && (message.launch.npcRevision != body.npc->intentRevision || body.npc->intent.targetNetworkId != message.targetNetworkId)) {
            return;
        }

        const auto player = _players.find(shooter->GetNetworkID());

        if (player == _players.end()) {
            return;
        }

        const auto shot       = player->second.shots.find(message.launch.sequence);
        const char *rejection = "unknown-or-ended-shot";

        if (shot == player->second.shots.end() || !shot->second.TryClaimImpact(message, victim.Position(), MafiaNet::GetTime(), &rejection)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->debug("Missile hit rejected: shooter={} sequence={} reason={} part={} targetDistance={:.2f}", shooter->GetNetworkID(), message.launch.sequence, rejection, message.bodyMaterial, glm::distance(message.position, victim.Position()));

            return;
        }

        auto relay = message;

        relay.launch                                                    = shot->second.launch; // Sender cannot replace the origin or shooter.
        _owedDamage[{shooter->GetNetworkID(), message.launch.sequence}] = OwedDamage {target->GetNetworkID(), MafiaNet::GetTime() + kDamageClaimMs};
        net->SendRPC(relay, MafiaNet::ToGuid(target->ownerGUID));
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Missile hit accepted: shooter={} sequence={} target={}", relay.launch.shooterNetworkId, relay.launch.sequence, relay.targetNetworkId);
    }

    void Missile::OnRetained(const Shared::RPC::MissileRetained &message, MafiaNet::Packet *packet) {
        if (packet == nullptr || !message.IsSane()) {
            return;
        }

        const auto body   = Combatant::Sender(message.shooterNetworkId, MafiaNet::ToPeerGuid(packet->guid));
        const auto victim = Combatant::Find(message.targetNetworkId);
        auto *shooter     = body.entity;
        auto *target      = victim.entity;
        if (body.human == nullptr || !victim || target == shooter || (victim.npc != nullptr ? !MafiaNet::VirtualWorldsCanSee(target->GetVirtualWorld(), shooter->GetVirtualWorld()) : target->GetVirtualWorld() != shooter->GetVirtualWorld())) {
            return;
        }

        const auto player = _players.find(shooter->GetNetworkID());

        if (player == _players.end()) {
            return;
        }

        const auto shot = player->second.shots.find(message.sequence);

        if (shot == player->second.shots.end() || !shot->second.TryClaimRetained(message, MafiaNet::GetTime())) {
            return;
        }

        // The round lodged in that body: into the body's inventory, where the
        // game puts it, rather than lying where it struck.
        auto &inventory        = Server::Get()->GetInventory();
        auto *stock            = victim.npc != nullptr ? KCDC::Features::Npc::NpcInventoryModule::Get().Find(target->GetNetworkID()) : nullptr;
        auto *into             = victim.npc != nullptr ? (stock != nullptr && stock->Ready() ? &stock->Stock() : nullptr) : inventory.Find(target->GetNetworkID());
        const auto &ammunition = shot->second.launch.ammunition;
        const bool lodged      = inventory.Lodge(shooter->GetNetworkID(), Shared::Game::CryGuidToString(ammunition.low, ammunition.high), into);
        if (lodged && victim.npc != nullptr) {
            KCDC::Features::Npc::NpcInventoryModule::Get().Changed(target->GetNetworkID());
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Missile round lodged: shooter={} sequence={} target={} lodged={}", shooter->GetNetworkID(), message.sequence, message.targetNetworkId, lodged);
    }

    void Missile::Update() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (replication == nullptr) {
            return;
        }

        const std::uint64_t now = MafiaNet::GetTime();

        std::erase_if(_owedDamage, [now](const auto &entry) {
            return entry.second.expiresAt <= now;
        });
        std::erase_if(_players, [](const auto &entry) {
            return !Combatant::Find(entry.first);
        });

        for (auto &[id, player] : _players) {
            for (auto it = player.shots.begin(); it != player.shots.end();) {
                if (it->second.expiresAt > now) {
                    ++it;
                    continue;
                }

                Shared::RPC::MissileEnded end;

                end.shooterNetworkId = id;
                end.sequence         = it->first;

                if (const auto body = Combatant::Find(id)) {
                    KCDC::RelayToStreamingPeers(end, *body.entity);
                }

                it = player.shots.erase(it);
            }
        }
    }

    bool Missile::ClaimDamage(std::uint64_t shooterNetworkId, std::uint64_t sequence, std::uint64_t victimNetworkId, std::uint64_t now) {
        const auto owed = _owedDamage.find({shooterNetworkId, sequence});
        if (owed == _owedDamage.end() || owed->second.victimNetworkId != victimNetworkId || owed->second.expiresAt <= now) {
            return false;
        }
        _owedDamage.erase(owed);
        return true;
    }

    void Missile::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        std::erase_if(_players, [guid](const auto &entry) { return entry.second.guid == guid; });
    }
} // namespace KCDC::Features::Player
