#include "features/pickpocket/pickpocket_service.h"

#include "core/server.h"
#include "features/pickpocket/pickpocket_scripting.h"
#include "features/player/player_roster.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

namespace KCDC::Features::Pickpocket {
    namespace {
        constexpr const char *kLogger = "Pickpocket";

        /**
         * How far apart the two bodies may be when the charge begins, and when
         * the theft settles. The game's own reach is a hand's length plus
         * `PickpocketingDistance`; the slack is the replicated positions being
         * a moment behind the machines that own them.
         */
        constexpr float kMaxReach = KCDC::Features::Inventory::InventoryGameplay::kMaximumTheftReach;

        /**
         * The least time between two requests from one thief. The native
         * charge cannot restart this fast, so only a client sending them by
         * hand is slowed.
         */
        constexpr std::uint64_t kMinRequestIntervalMs = 750;

        /**
         * How long an attempt may stay open. The loot screen has no clock of
         * its own; this only ends one whose thief went silent.
         */
        constexpr std::uint64_t kThiefTimeoutMs = 180000;

        /** The catalog entry behind a class the wire named, or null for one the catalog does not have. */
        const Shared::Game::ItemClassEntry *FindEntry(const Shared::RPC::ItemClassId &itemClass) {
            return Shared::Game::FindItemClass(itemClass);
        }

        std::uint64_t Now() {
            return MafiaNet::GetTime();
        }
    } // namespace

    PickpocketService &PickpocketService::Get() {
        static PickpocketService instance;
        return instance;
    }

    void PickpocketService::Install() {
        if (_installed) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _installed = true;

        net->RegisterRPC<Shared::RPC::PickpocketRequest>([this](const Shared::RPC::PickpocketRequest &request, MafiaNet::Packet *packet) {
            OnRequest(request, packet);
        });
        net->RegisterRPC<Shared::RPC::PickpocketResult>([this](const Shared::RPC::PickpocketResult &result, MafiaNet::Packet *packet) {
            OnResult(result, packet);
        });
    }

    void PickpocketService::OnRequest(const Shared::RPC::PickpocketRequest &request, MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (packet == nullptr || !request.IsSane() || replication == nullptr || net == nullptr) {
            return;
        }
        auto *thief = replication->GetViewerAs<Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        if (thief == nullptr) {
            return;
        }
        const std::uint64_t thiefId = thief->GetNetworkID();
        const std::uint64_t now     = Now();
        auto [last, first]          = _lastRequestAt.try_emplace(thiefId, now);
        if (!first && now - last->second < kMinRequestIntervalMs) {
            SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::Unavailable, {});
            return;
        }
        last->second = now;

        // A thief starting again has let go of whatever it was doing before.
        for (auto it = _attempts.begin(); it != _attempts.end();) {
            it = it->second.thiefId == thiefId ? _attempts.erase(it) : std::next(it);
        }

        Shared::Entities::HumanEntity *victim = Player::Players::Find(request.victimNetworkId);
        if (victim == nullptr || victim == thief || IsVictim(request.victimNetworkId)) {
            SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::Unavailable, {});
            return;
        }
        if (glm::distance(thief->position, victim->position) > kMaxReach) {
            SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::TooFar, {});
            return;
        }
        if (!Scripting::Events::PlayerPickpocketStart(thiefId, request.victimNetworkId)) {
            SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::Refused, {});
            return;
        }

        auto &inventories     = Server::Get()->GetInventory();
        const auto *pockets   = inventories.Find(request.victimNetworkId);
        const auto *equipment = inventories.Equipped(request.victimNetworkId);
        if (pockets == nullptr || equipment == nullptr || inventories.Find(thiefId) == nullptr) {
            SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::Unavailable, {});
            return;
        }

        Attempt attempt;
        attempt.attemptId = _nextAttempt++;
        if (_nextAttempt == 0) {
            _nextAttempt = 1;
        }
        attempt.requestId = request.requestId;
        attempt.thiefId   = thiefId;
        attempt.victimId  = request.victimNetworkId;
        attempt.offeredAt = now;
        attempt.deadline  = now + kThiefTimeoutMs;

        // What the thief's grid may show: unworn units, one line per class the
        // wire can name, as many lines as it carries.
        std::vector<Shared::RPC::PickpocketItem> stock;
        for (const auto &line : KCDC::Features::Inventory::InventoryGameplay::Pockets(*pockets, *equipment)) {
            if (stock.size() >= Shared::RPC::kMaxPickpocketStock) {
                break;
            }
            const Shared::RPC::ItemClassId itemClass = Shared::Game::ItemClassIdFromGuid(line.itemClass);
            if (!itemClass.IsValid()) {
                continue;
            }
            const std::uint32_t amount = std::min<std::uint32_t>(line.amount, Shared::RPC::kMaxGiveAmount);
            stock.push_back({itemClass, amount});
            attempt.offer.push_back({line.itemClass, amount});
        }

        Framework::Logging::GetLogger(kLogger)->info("'{}' is picking the pocket of '{}' (attempt {})", thief->nickname, victim->nickname, attempt.attemptId);
        _attempts.insert_or_assign(attempt.attemptId, std::move(attempt));
        SendVerdict(thiefId, request.requestId, Shared::RPC::PickpocketRefusal::None, stock);
    }

    void PickpocketService::OnResult(const Shared::RPC::PickpocketResult &result, MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (packet == nullptr || !result.IsSane() || replication == nullptr || net == nullptr) {
            return;
        }
        auto *thief = replication->GetViewerAs<Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        if (thief == nullptr) {
            return;
        }
        Attempt *found = FindByThief(thief->GetNetworkID(), result.requestId);
        if (found == nullptr) {
            return;
        }

        // Over; forgotten before scripts hear of it, so a handler never finds it.
        const Attempt attempt = *found;
        _attempts.erase(found->attemptId);

        const auto outcome = static_cast<Shared::RPC::PickpocketOutcome>(result.outcome);
        switch (outcome) {
        case Shared::RPC::PickpocketOutcome::Stole: Settle(attempt, result.taken, Now()); break;
        case Shared::RPC::PickpocketOutcome::Caught: Scripting::Events::PlayerPickpocketCaught(attempt.thiefId, attempt.victimId); break;
        case Shared::RPC::PickpocketOutcome::Abandoned: Framework::Logging::GetLogger(kLogger)->info("Attempt {} ended without a take", attempt.attemptId); break;
        }
    }

    void PickpocketService::Settle(const Attempt &attempt, const std::vector<Shared::RPC::PickpocketItem> &claim, std::uint64_t now) {
        // The thief's own word that it stole: held against how long the
        // charge takes and where the two bodies are now.
        const auto *thiefBody  = Player::Players::Find(attempt.thiefId);
        const auto *victimBody = Player::Players::Find(attempt.victimId);
        const auto refusal     = thiefBody == nullptr || victimBody == nullptr ? std::string("unavailable") : KCDC::Features::Inventory::InventoryGameplay::TheftRefusal(attempt.offeredAt, now, thiefBody->position, thiefBody->GetVirtualWorld(), victimBody->position, victimBody->GetVirtualWorld());
        if (!refusal.empty()) {
            Framework::Logging::GetLogger(kLogger)->warn("Attempt {} theft refused: {}", attempt.attemptId, refusal);
            Scripting::Events::PlayerPickpocketed(attempt.thiefId, attempt.victimId, {});
            return;
        }
        auto &inventories     = Server::Get()->GetInventory();
        auto *victim          = inventories.Find(attempt.victimId);
        auto *thief           = inventories.Find(attempt.thiefId);
        const auto *equipment = inventories.Equipped(attempt.victimId);
        std::vector<KCDC::Features::Inventory::PocketLine> lines;
        std::vector<StolenLine> moved;
        for (const Shared::RPC::PickpocketItem &line : claim) {
            const Shared::Game::ItemClassEntry *entry = FindEntry(line.itemClass);
            if (entry == nullptr) {
                lines.clear();
                break;
            }
            lines.push_back({std::string(entry->guid), line.amount});
            moved.push_back(StolenLine {entry, line.amount});
        }
        if (lines.empty() || victim == nullptr || thief == nullptr || equipment == nullptr) {
            Scripting::Events::PlayerPickpocketed(attempt.thiefId, attempt.victimId, {});
            return;
        }
        const auto result = KCDC::Features::Inventory::InventoryGameplay::Steal(*victim, *thief, *equipment, attempt.offer, lines);
        if (!result.ok) {
            Framework::Logging::GetLogger(kLogger)->warn("Attempt {} moved nothing: {}", attempt.attemptId, result.code);
            Scripting::Events::PlayerPickpocketed(attempt.thiefId, attempt.victimId, {});
            return;
        }
        inventories.DispatchChanges();
        Scripting::Events::PlayerPickpocketed(attempt.thiefId, attempt.victimId, moved);
    }

    void PickpocketService::Update() {
        // A thief that never reported has nobody left to tell.
        const std::uint64_t now = Now();
        std::erase_if(_attempts, [now](const auto &entry) {
            return now >= entry.second.deadline;
        });
    }

    void PickpocketService::Forget(std::uint64_t playerNetworkId) {
        // The thief's client ends its own attempt as the victim's body goes.
        _lastRequestAt.erase(playerNetworkId);
        std::erase_if(_attempts, [playerNetworkId](const auto &entry) {
            return entry.second.thiefId == playerNetworkId || entry.second.victimId == playerNetworkId;
        });
    }

    void PickpocketService::SendVerdict(std::uint64_t thiefId, std::uint32_t requestId, Shared::RPC::PickpocketRefusal refusal, const std::vector<Shared::RPC::PickpocketItem> &items) const {
        auto *net                            = Framework::CoreModules::GetNetworkPeer();
        Shared::Entities::HumanEntity *thief = Player::Players::Find(thiefId);
        if (net == nullptr || thief == nullptr) {
            return;
        }
        Shared::RPC::PickpocketVerdict verdict;
        verdict.requestId = requestId;
        verdict.refusal   = static_cast<std::uint8_t>(refusal);
        verdict.items     = items;
        net->SendRPC(verdict, MafiaNet::ToGuid(thief->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    PickpocketService::Attempt *PickpocketService::FindByThief(std::uint64_t thiefId, std::uint32_t requestId) {
        for (auto &[attemptId, attempt] : _attempts) {
            if (attempt.thiefId == thiefId && attempt.requestId == requestId) {
                return &attempt;
            }
        }
        return nullptr;
    }

    bool PickpocketService::IsVictim(std::uint64_t playerId) const {
        return std::any_of(_attempts.begin(), _attempts.end(), [playerId](const auto &entry) {
            return entry.second.victimId == playerId;
        });
    }
} // namespace KCDC::Features::Pickpocket
