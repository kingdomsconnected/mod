#include "player_item_requests.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <utility>

namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "InventorySync";

        // As long as a world query waits, and for the same reasons.
        constexpr std::uint64_t kAnswerTimeout = 5000;

        // One result answers one request, so an inbox this deep is a peer talking unprompted.
        constexpr std::size_t kMaxIncomingResults = 1024;

        /** What an unanswered request settles with. */
        ItemRequests::Outcome Refusal(std::uint32_t requested, std::string reason) {
            ItemRequests::Outcome refused;
            refused.requested = requested;
            refused.reason    = std::move(reason);
            return refused;
        }
    } // namespace

    void ItemRequests::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Item request RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<Shared::RPC::TakeItemResult>([this](const Shared::RPC::TakeItemResult &result, MafiaNet::Packet *packet) {
            QueueResult(result, packet);
        });
    }

    void ItemRequests::QueueResult(const Shared::RPC::TakeItemResult &result, MafiaNet::Packet *packet) {
        if (packet == nullptr || !result.IsSane()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incomingResults.size() < kMaxIncomingResults) {
            _incomingResults.emplace_back(result, MafiaNet::ToPeerGuid(packet->guid));
        }
    }

    bool ItemRequests::Take(Shared::Entities::HumanEntity &player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount, OutcomeHandler handler) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return false;
        }

        // Refused here rather than sent and waited on, since the receiver would
        // drop it. The id is the one field still unset, and a refusal spends none.
        Shared::RPC::TakeItem take;
        take.SetGuid(item.guid);
        take.amount    = amount;
        take.requestId = 1;
        if (!take.IsSane()) {
            return false;
        }

        take.requestId = _nextRequestId++;
        if (_nextRequestId == 0) {
            _nextRequestId = 1;
        }

        Pending pending;
        pending.sender    = player.ownerGUID;
        pending.expiresAt = MafiaNet::GetTime() + kAnswerTimeout;
        pending.requested = amount;
        pending.handler   = std::move(handler);
        _pending.insert_or_assign(take.requestId, std::move(pending));

        net->SendRPC(take, MafiaNet::ToGuid(player.ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    void ItemRequests::Settle(std::uint64_t requestId, Outcome outcome) {
        const auto pending = _pending.find(requestId);
        if (pending == _pending.end()) {
            return;
        }

        // Dropped before the handler runs, so a handler that asks again cannot re-enter this one.
        const OutcomeHandler handler = std::move(pending->second.handler);
        _pending.erase(pending);
        if (handler) {
            handler(outcome);
        }
    }

    void ItemRequests::ResolveResult(const Shared::RPC::TakeItemResult &result, MafiaNet::PeerGuid sender) {
        const auto pending = _pending.find(result.requestId);
        if (pending == _pending.end() || pending->second.sender != sender) {
            return;
        }

        const std::uint32_t requested = pending->second.requested;
        if (!result.answered) {
            Settle(result.requestId, Refusal(requested, "Your client has no inventory loaded right now."));
            return;
        }

        // Out of step or making it up; either way the number means nothing.
        if (result.removed > requested) {
            Framework::Logging::GetLogger(kLogger)->warn("Take request {} was answered with {} removed of a requested {}", result.requestId, result.removed, requested);
            Settle(result.requestId, Refusal(requested, "Your client answered with an impossible amount."));
            return;
        }

        Outcome settled;
        settled.answered  = true;
        settled.removed   = result.removed;
        settled.requested = requested;
        Settle(result.requestId, std::move(settled));
    }

    void ItemRequests::Update() {
        std::vector<std::pair<Shared::RPC::TakeItemResult, MafiaNet::PeerGuid>> results;
        {
            std::scoped_lock lock(_incomingMutex);
            results.swap(_incomingResults);
        }

        for (const auto &result : results) {
            ResolveResult(result.first, result.second);
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
            Settle(requestId, Refusal(pending != _pending.end() ? pending->second.requested : 0, "Your client did not answer in time."));
        }
    }

    void ItemRequests::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        std::vector<std::uint64_t> abandoned;
        for (const auto &entry : _pending) {
            if (entry.second.sender == guid) {
                abandoned.push_back(entry.first);
            }
        }
        for (const std::uint64_t requestId : abandoned) {
            const auto pending = _pending.find(requestId);
            Settle(requestId, Refusal(pending != _pending.end() ? pending->second.requested : 0, "You left before your client answered."));
        }
    }
} // namespace KCDC::Features::Player
