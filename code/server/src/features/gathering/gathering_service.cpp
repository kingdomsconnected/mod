#include "gathering_service.h"

#include "core/server.h"
#include "features/gathering/gathering_scripting.h"
#include "features/player/player_roster.h"
#include "shared/features/gathering/gathering_message.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/gathering_catalog.h"
#include "shared/game/progression_catalog.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <vector>

namespace KCDC::Features::Gathering {
    namespace {
        constexpr std::size_t kMaximumRequestBytes = 2048;
        constexpr std::size_t kPointsPerMessage    = 256;
        constexpr std::size_t kMessagesPerPeerTick = 32;
        constexpr std::uint64_t kPruneIntervalMs   = 60000;

        std::uint64_t NowMs() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        std::uint64_t WorldMs() {
            return Server::Get()->GetWorld().GetWorldTimeMilliseconds();
        }

        GatheringHooks Hooks() {
            GatheringHooks hooks;
            hooks.inventory = [](std::uint64_t playerId) {
                auto &inventories = Server::Get()->GetInventory();
                return inventories.IsReady(playerId) ? inventories.Find(playerId) : nullptr;
            };
            hooks.harvesting = [](std::uint64_t playerId, const nlohmann::json &proposal) {
                return KCDC::Scripting::GatheringHarvest(playerId, proposal);
            };
            hooks.experience = [](std::uint64_t playerId, double xp) {
                auto *human     = Features::Player::Players::Find(playerId);
                const int track = KCDC::Shared::Game::FindProgressionTrack("survival");
                if (human != nullptr && track >= 0) {
                    Server::Get()->GetProgression().AddXp(human, static_cast<std::uint8_t>(track), xp, true);
                }
            };
            hooks.survivalLevel = [](std::uint64_t playerId) -> std::optional<float> {
                const auto *digest = Server::Get()->GetProgression().Accepted(playerId);
                const int track    = KCDC::Shared::Game::FindProgressionTrack("survival");
                if (digest == nullptr || track < 0) {
                    return std::nullopt;
                }
                return static_cast<float>(digest->tracks[track].level);
            };
            return hooks;
        }
    } // namespace

    GatheringService::GatheringService(): _gathering(Hooks()) {}

    void GatheringService::Install(const std::string &level) {
        _gathering.SetSources(KCDC::Shared::Game::GatheringSources(level));
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::GatheringRequest>([this](const KCDC::Shared::RPC::GatheringRequest &message, MafiaNet::Packet *packet) {
            if (packet != nullptr && message.valid && message.body.size() <= kMaximumRequestBytes) {
                Receive(MafiaNet::ToPeerGuid(packet->guid), message.body);
            }
        });
    }

    void GatheringService::Receive(MafiaNet::PeerGuid peer, const std::string &body) {
        auto *player = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        GatheringRequest request;
        if (player == nullptr || !GatheringModule::ReadRequest(body, request)) {
            return;
        }
        const auto playerId = player->GetNetworkID();
        const auto world    = player->GetVirtualWorld();
        const auto worldMs  = WorldMs();
        if (request.action == "sync") {
            if (auto points = _gathering.Sync(playerId, world, player->position, NowMs(), worldMs, request.full)) {
                SendPoints(peer, std::move(*points), false, request.full, request.syncGeneration);
            }
            return;
        }
        if (request.action == "begin") {
            const auto code = _gathering.Begin(playerId, request, player->position, world, NowMs(), worldMs);
            if (code == "depleted") {
                SendPoints(peer, {*_gathering.FindDepleted(world, request.kind, request.position, worldMs)});
            }
            Send(peer, {{"id", request.id}, {"status", code.empty() ? "started" : "refused"}, {"code", code}});
            return;
        }
        const auto harvest = _gathering.Complete(playerId, request, player->position, world, player->soul.CanAct(), NowMs(), worldMs);
        if (!harvest.code.empty()) {
            SendPoints(peer, harvest.depleted);
            Send(peer, {{"id", request.id}, {"status", "refused"}, {"code", harvest.code}});
            return;
        }
        // Everyone whose area covers the plants hides them, so the same ones
        // are not offered twice.
        Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *viewer) {
            if (viewer->GetVirtualWorld() == world && GatheringModule::Covers(viewer->position, harvest.position) && viewer->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                SendPoints(viewer->ownerGUID, harvest.depleted);
            }
        });

        // Queue the harvest before scripts reacting to inventory changes can
        // remove or restore the same source. Their correction must arrive last.
        Server::Get()->GetInventory().DispatchChanges();
        Send(peer, {{"id", request.id}, {"status", "granted"}, {"code", ""}, {"amount", harvest.amount}});

        const auto *kind       = KCDC::Shared::Game::FindGatheringKind(harvest.kind);
        nlohmann::json outputs = nlohmann::json::array();
        for (const auto &unit : harvest.outputs) {
            outputs.push_back({{"id", unit.id}, {"amount", unit.amount}});
        }
        KCDC::Scripting::EmitGatheringHarvested(playerId,
            {{"item", kind != nullptr ? std::string(kind->itemClass) : std::string()}, {"amount", harvest.amount}, {"kind", harvest.kind}, {"position", {harvest.position.x, harvest.position.y, harvest.position.z}}, {"virtualWorld", world}, {"items", std::move(outputs)}});
    }

    void GatheringService::Send(MafiaNet::PeerGuid peer, nlohmann::json payload) {
        const auto *viewer = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        if (viewer != nullptr) {
            _outgoing[peer].push_back({viewer->GetVirtualWorld(), nullptr, std::move(payload)});
        }
    }

    void GatheringService::SendPoints(MafiaNet::PeerGuid peer, std::vector<GatheringPoint> points, bool respawn, bool full, std::uint64_t syncGeneration) {
        const auto *viewer = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        if (viewer != nullptr) {
            _outgoing[peer].push_back({viewer->GetVirtualWorld(), std::make_shared<const std::vector<GatheringPoint>>(std::move(points)), {}, 0, respawn, full, syncGeneration});
        }
    }

    void GatheringService::BroadcastPoints(std::uint32_t virtualWorld, std::vector<GatheringPoint> points, bool respawn) {
        if (points.empty()) {
            return;
        }
        // A large restoration has one backing vector, shared by its recipients.
        const auto shared = std::make_shared<const std::vector<GatheringPoint>>(std::move(points));
        Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *viewer) {
            if (viewer->GetVirtualWorld() == virtualWorld && viewer->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                _outgoing[viewer->ownerGUID].push_back({virtualWorld, shared, {}, 0, respawn, false});
            }
        });
    }

    void GatheringService::FlushDeliveries() {
        for (auto peer = _outgoing.begin(); peer != _outgoing.end();) {
            const auto *viewer = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer->first);
            if (viewer == nullptr) {
                peer = _outgoing.erase(peer);
                continue;
            }
            auto &queue = peer->second;
            std::erase_if(queue, [&](const Delivery &delivery) {
                return delivery.virtualWorld != viewer->GetVirtualWorld();
            });
            for (std::size_t sent = 0; sent < kMessagesPerPeerTick && !queue.empty(); ++sent) {
                auto &delivery = queue.front();
                nlohmann::json payload;
                bool complete = true;
                if (delivery.points) {
                    nlohmann::json chunk = nlohmann::json::array();
                    const auto end       = std::min(delivery.offset + kPointsPerMessage, delivery.points->size());
                    for (; delivery.offset < end; ++delivery.offset) {
                        const auto &point = (*delivery.points)[delivery.offset];
                        chunk.push_back({{"kind", point.kind}, {"position", {point.position.x, point.position.y, point.position.z}}, {"until", point.until}});
                    }
                    complete = delivery.offset == delivery.points->size();
                    payload  = {{"points", std::move(chunk)}, {"respawn", delivery.respawn}, {"full", delivery.full && complete}};
                    if (delivery.full) {
                        payload["syncGeneration"] = delivery.syncGeneration;
                    }
                }
                else {
                    payload = std::move(delivery.payload);
                }
                payload["virtualWorld"] = delivery.virtualWorld;
                KCDC::Shared::RPC::GatheringState message;
                message.body = payload.dump();
                Framework::CoreModules::GetNetworkPeer()->SendRPC(message, MafiaNet::ToGuid(peer->first), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                if (complete) {
                    queue.pop_front();
                }
            }
            peer = queue.empty() ? _outgoing.erase(peer) : std::next(peer);
        }
    }

    bool GatheringService::SetRespawnTime(double minHours, double maxHours) {
        if (!_gathering.SetRespawnTime(minHours, maxHours)) {
            return false;
        }
        for (auto &[world, points] : _gathering.Reschedule(WorldMs())) {
            BroadcastPoints(world, std::move(points), false);
        }
        return true;
    }

    void GatheringService::ResetRespawnTime() {
        _gathering.ResetRespawnTime();
        for (auto &[world, points] : _gathering.Reschedule(WorldMs())) {
            BroadcastPoints(world, std::move(points), false);
        }
    }

    std::size_t GatheringService::Respawn(std::uint32_t virtualWorld, const std::optional<glm::vec3> &position, float radius) {
        auto points      = _gathering.Respawn(virtualWorld, position, radius, WorldMs());
        const auto count = points.size();
        BroadcastPoints(virtualWorld, std::move(points), true);
        return count;
    }

    std::vector<KCDC::Shared::Game::GatheringSource> GatheringService::GetHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) const {
        return _gathering.GetHerbs(Server::Get()->GetAreas(), area, virtualWorld, WorldMs());
    }

    std::size_t GatheringService::RemoveHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) {
        const auto points = _gathering.RemoveHerbs(Server::Get()->GetAreas(), area, virtualWorld, WorldMs());
        // As with ordinary picks, late arrivals receive these through Sync.
        Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *viewer) {
            if (viewer->GetVirtualWorld() != virtualWorld || viewer->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                return;
            }
            std::vector<GatheringPoint> nearby;
            for (const auto &point : points) {
                if (GatheringModule::Covers(viewer->position, point.position)) {
                    nearby.push_back(point);
                }
            }
            SendPoints(viewer->ownerGUID, std::move(nearby));
        });
        return points.size();
    }

    std::size_t GatheringService::RespawnHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) {
        auto points      = _gathering.RespawnHerbs(Server::Get()->GetAreas(), area, virtualWorld, WorldMs());
        const auto count = points.size();
        BroadcastPoints(virtualWorld, std::move(points), true);
        return count;
    }

    void GatheringService::Update() {
        Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *viewer) {
            if (viewer->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                return;
            }
            const auto world             = viewer->GetVirtualWorld();
            const auto [entry, inserted] = _playerWorlds.try_emplace(viewer->GetNetworkID(), world);
            if (!inserted && entry->second != world) {
                entry->second = world;
                SendPoints(viewer->ownerGUID, {});
            }
        });
        FlushDeliveries();
        const auto now = NowMs();
        if (now - _lastPruneMs >= kPruneIntervalMs) {
            _lastPruneMs = now;
            _gathering.Prune(WorldMs());
        }
    }

    void GatheringService::Reset() {
        _playerWorlds.clear();
        _outgoing.clear();
        _gathering.Regrow();
    }

    void GatheringService::OnPlayerDisconnect(MafiaNet::PeerGuid peer) {
        _outgoing.erase(peer);
        if (auto *player = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer)) {
            _playerWorlds.erase(player->GetNetworkID());
            _gathering.Forget(player->GetNetworkID());
        }
    }
} // namespace KCDC::Features::Gathering
