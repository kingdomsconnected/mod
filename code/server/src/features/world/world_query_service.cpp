#include "world_query_service.h"

#include "features/world/world_query_scripting.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <mutex>
#include <utility>

namespace KCDC::Features::World {
    namespace {
        constexpr const char *kLogger = "WorldQuery";

        // As a nearest-door query waits. A client silent this long is
        // loading, paused on a menu or gone.
        constexpr std::uint64_t kAnswerTimeout = 5000;

        // One answer per message, and the server only asks what a resource
        // asked for: an inbox this deep is a peer talking unprompted.
        constexpr std::size_t kMaxIncomingAnswers = 1024;

        // A swing reaches a couple of metres; this leaves room for a long weapon and a body a tick behind.
        constexpr float kMaxWeaponReach = 8.0F;

        // A swing touches a handful of things, and a player swings a few times a second at most.
        constexpr std::uint32_t kMaxWeaponHitsPerSecond = 16;
        constexpr std::size_t kMaxIncomingWeaponHits    = 1024;

        /** What an unanswered query settles with, stamped with the kind it asked. */
        WorldQueryService::Answer Refusal(KCDC::Shared::RPC::WorldQueryKind kind, std::string reason) {
            WorldQueryService::Answer refused;
            refused.kind   = kind;
            refused.reason = std::move(reason);
            return refused;
        }
    } // namespace

    void WorldQueryService::Install(const std::string &level) {
        _level     = level;
        _levelHash = KCDC::Shared::RPC::LevelHash(level);

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("World query RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::WorldQueryAnswer>([this](const KCDC::Shared::RPC::WorldQueryAnswer &answer, MafiaNet::Packet *packet) {
            QueueAnswer(answer, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::WeaponHitReport>([this](const KCDC::Shared::RPC::WeaponHitReport &report, MafiaNet::Packet *packet) {
            QueueWeaponHit(report, packet);
        });
    }

    void WorldQueryService::QueueWeaponHit(const KCDC::Shared::RPC::WeaponHitReport &report, MafiaNet::Packet *packet) {
        if (packet == nullptr || !report.IsSane()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incomingWeaponHits.size() < kMaxIncomingWeaponHits) {
            _incomingWeaponHits.emplace_back(report, MafiaNet::ToPeerGuid(packet->guid));
        }
    }

    void WorldQueryService::ResolveWeaponHit(const KCDC::Shared::RPC::WeaponHitReport &report, MafiaNet::PeerGuid sender) {
        if (report.levelHash != _levelHash) {
            return;
        }

        KCDC::Shared::Entities::HumanEntity *player = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *human) {
                if (player == nullptr && human->ownerGUID == sender) {
                    player = human;
                }
            });
        }
        const glm::vec3 point(report.hit.pointX, report.hit.pointY, report.hit.pointZ);
        if (player == nullptr || !player->transformReady || glm::distance(point, player->position) > kMaxWeaponReach) {
            return;
        }

        const std::uint64_t now = MafiaNet::GetTime();
        WeaponHitBudget &budget = _weaponHitBudgets[sender];
        if (now - budget.windowStart >= 1000) {
            budget.windowStart = now;
            budget.count       = 0;
        }
        if (++budget.count > kMaxWeaponHitsPerSecond) {
            return;
        }
        KCDC::Scripting::Events::PlayerWeaponHit(player->GetNetworkID(), report.hit);
    }

    void WorldQueryService::QueueAnswer(const KCDC::Shared::RPC::WorldQueryAnswer &answer, MafiaNet::Packet *packet) {
        if (packet == nullptr || !answer.IsSane()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incomingAnswers.size() < kMaxIncomingAnswers) {
            _incomingAnswers.emplace_back(answer, MafiaNet::ToPeerGuid(packet->guid));
        }
    }

    bool WorldQueryService::Ask(KCDC::Shared::Entities::HumanEntity &caller, KCDC::Shared::RPC::WorldQuery query, AnswerHandler handler) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return false;
        }

        query.requestId = _nextRequestId++;
        if (_nextRequestId == 0) {
            _nextRequestId = 1;
        }

        Pending pending;
        pending.sender    = caller.ownerGUID;
        pending.expiresAt = MafiaNet::GetTime() + kAnswerTimeout;
        pending.kind      = query.kind;
        pending.handler   = std::move(handler);
        _pending.insert_or_assign(query.requestId, std::move(pending));

        net->SendRPC(query, MafiaNet::ToGuid(caller.ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    void WorldQueryService::Settle(std::uint64_t requestId, Answer answer) {
        const auto pending = _pending.find(requestId);
        if (pending == _pending.end()) {
            return;
        }

        // Dropped before the handler runs: it is script, and it can ask again.
        const AnswerHandler handler = std::move(pending->second.handler);
        _pending.erase(pending);
        if (handler) {
            handler(answer);
        }
    }

    void WorldQueryService::ResolveAnswer(const KCDC::Shared::RPC::WorldQueryAnswer &answer, MafiaNet::PeerGuid sender) {
        const auto pending = _pending.find(answer.requestId);
        if (pending == _pending.end() || pending->second.sender != sender) {
            return;
        }

        // Out of step, or making it up; either way the answer means nothing.
        if (answer.kind != pending->second.kind) {
            Settle(answer.requestId, Refusal(pending->second.kind, "Your client answered a different question."));
            return;
        }
        if (!answer.answered) {
            Settle(answer.requestId, Refusal(answer.kind, "Your client has no world loaded right now."));
            return;
        }
        if (answer.levelHash != _levelHash) {
            Settle(answer.requestId, Refusal(answer.kind, "Your client is in another level."));
            return;
        }

        Answer settled;
        settled.answered = true;
        settled.kind     = answer.kind;
        settled.hits     = answer.hits;
        settled.entities = answer.entities;
        Settle(answer.requestId, std::move(settled));
    }

    void WorldQueryService::Update() {
        std::vector<std::pair<KCDC::Shared::RPC::WorldQueryAnswer, MafiaNet::PeerGuid>> answers;
        std::vector<std::pair<KCDC::Shared::RPC::WeaponHitReport, MafiaNet::PeerGuid>> weaponHits;
        {
            std::scoped_lock lock(_incomingMutex);
            answers.swap(_incomingAnswers);
            weaponHits.swap(_incomingWeaponHits);
        }

        for (const auto &answer : answers) {
            ResolveAnswer(answer.first, answer.second);
        }
        for (const auto &weaponHit : weaponHits) {
            ResolveWeaponHit(weaponHit.first, weaponHit.second);
        }

        const std::uint64_t now = MafiaNet::GetTime();
        std::vector<std::uint64_t> expired;
        for (const auto &entry : _pending) {
            if (now >= entry.second.expiresAt) {
                expired.push_back(entry.first);
            }
        }
        for (const std::uint64_t requestId : expired) {
            const auto pending = _pending.find(requestId);
            Settle(requestId, Refusal(pending != _pending.end() ? pending->second.kind : KCDC::Shared::RPC::WorldQueryKind::Raycast, "Your client did not answer in time."));
        }
    }

    void WorldQueryService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        _weaponHitBudgets.erase(guid);
        std::vector<std::uint64_t> abandoned;
        for (const auto &entry : _pending) {
            if (entry.second.sender == guid) {
                abandoned.push_back(entry.first);
            }
        }
        for (const std::uint64_t requestId : abandoned) {
            const auto pending = _pending.find(requestId);
            Settle(requestId, Refusal(pending != _pending.end() ? pending->second.kind : KCDC::Shared::RPC::WorldQueryKind::Raycast, "You left before your client answered."));
        }
    }
} // namespace KCDC::Features::World
