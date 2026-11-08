#include "features/dice/dice_service.h"

#include "features/dice/dice_scripting.h"
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
#include <utility>

namespace KCDC::Features::Dice {
    namespace {
        constexpr const char *kLogger = "Dice";

        /**
         * How far apart two players may stand to start a match. Both clients
         * seat the two bodies on the dice table nearest the point between
         * them, so they must already be standing at one.
         */
        constexpr float kMaxSeparation = 6.0F;

        /** The score a match is played to when a script names none: the game's own most common target. */
        constexpr std::uint32_t kDefaultTargetScore = 2000;
        constexpr std::uint32_t kMaxTargetScore     = 100000;

        /**
         * How long the seat to play may take. The native table nags after a
         * fraction of this; this only ends a match whose player walked away
         * from the keyboard, or whose client stopped answering.
         */
        constexpr std::uint64_t kTurnTimeoutMs = 180000;

        std::uint64_t Now() {
            return MafiaNet::GetTime();
        }

        const char *ReasonName(Shared::Game::Dice::EndReason reason) {
            switch (reason) {
            case Shared::Game::Dice::EndReason::Won: return "won";
            case Shared::Game::Dice::EndReason::GaveUp: return "gaveUp";
            case Shared::Game::Dice::EndReason::Left: return "left";
            case Shared::Game::Dice::EndReason::TimedOut: return "timedOut";
            case Shared::Game::Dice::EndReason::Failed: return "failed";
            case Shared::Game::Dice::EndReason::Stopped: return "stopped";
            }
            return "stopped";
        }
    } // namespace

    DiceService &DiceService::Get() {
        static DiceService instance;
        return instance;
    }

    void DiceService::Install() {
        if (_installed) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _installed = true;

        net->RegisterRPC<Shared::RPC::DiceMove>([this](const Shared::RPC::DiceMove &move, MafiaNet::Packet *packet) {
            OnMove(move, packet);
        });
        net->RegisterRPC<Shared::RPC::DiceLeave>([this](const Shared::RPC::DiceLeave &leave, MafiaNet::Packet *packet) {
            OnLeave(leave, packet);
        });
    }

    std::uint32_t DiceService::Start(std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint32_t targetScore, std::string &error, const std::array<std::string, 2> &names) {
        if (!Shared::RPC::DiceDetail::ValidName(names[0]) || !Shared::RPC::DiceDetail::ValidName(names[1])) {
            error = "names must contain at most 64 UTF-8 bytes and no control characters";
            return 0;
        }
        Shared::Entities::HumanEntity *first  = Player::Players::Find(firstNetworkId);
        Shared::Entities::HumanEntity *second = Player::Players::Find(secondNetworkId);
        if (first == nullptr || second == nullptr) {
            error = "both players must be connected";
            return 0;
        }
        if (first == second) {
            error = "a player cannot play against themselves";
            return 0;
        }
        if (MatchOf(firstNetworkId) != 0 || MatchOf(secondNetworkId) != 0) {
            error = "one of the players is already playing dice";
            return 0;
        }
        if (first->GetVirtualWorld() != second->GetVirtualWorld() || glm::distance(first->position, second->position) > kMaxSeparation) {
            error = "the players must stand together at a dice table";
            return 0;
        }
        if (targetScore == 0) {
            targetScore = kDefaultTargetScore;
        }
        if (targetScore > kMaxTargetScore) {
            error = "the target score is at most " + std::to_string(kMaxTargetScore);
            return 0;
        }

        const std::uint32_t matchId = _nextMatch++;
        if (_nextMatch == 0) {
            _nextMatch = 1;
        }
        const std::uint8_t firstSeat = static_cast<std::uint8_t>(std::uniform_int_distribution<int>(0, 1)(_random));
        Match match(targetScore, firstSeat);
        match.matchId  = matchId;
        match.players  = {firstNetworkId, secondNetworkId};
        match.deadline = Now() + kTurnTimeoutMs;
        QueueProgress(match, "diceTurnStart", firstSeat);
        const std::vector<Shared::RPC::DiceDeal> deals = DealPending(match);
        if (match.table.ThrowNeeded()) {
            error = "the opening throws could not be dealt";
            return 0;
        }

        // The anchor both clients search for the table around.
        const glm::vec3 anchor = (first->position + second->position) * 0.5F;
        for (std::uint8_t seat = 0; seat < 2; ++seat) {
            Shared::Entities::HumanEntity *player = seat == 0 ? first : second;
            Shared::RPC::DiceMatchStart start;
            start.matchId           = matchId;
            start.localSeat         = seat;
            start.firstSeat         = firstSeat;
            start.opponentNetworkId = match.players[seat ^ 1];
            start.targetScore       = targetScore;
            start.anchorX           = anchor.x;
            start.anchorY           = anchor.y;
            start.anchorZ           = anchor.z;
            start.deals             = deals;
            start.names             = names;
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->SendRPC(start, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("Match {}: '{}' against '{}' to {}, seat {} throws first", matchId, first->nickname, second->nickname, targetScore, firstSeat);
        _events.push_back([=] {
            Scripting::Events::DiceMatchStart(matchId, firstNetworkId, secondNetworkId, targetScore);
        });
        PublishProgress(match);
        _matches.emplace(matchId, std::move(match));
        FlushEvents();
        return matchId;
    }

    bool DiceService::SetNames(std::uint32_t matchId, const std::array<std::string, 2> &names) {
        Shared::RPC::DiceNames report {matchId, names};
        const auto found = _matches.find(matchId);
        if (!report.IsSane() || found == _matches.end()) {
            return false;
        }
        SendToSeats(found->second, report);
        return true;
    }

    bool DiceService::Stop(std::uint32_t matchId) {
        if (_matches.find(matchId) == _matches.end()) {
            return false;
        }
        End(matchId, Shared::Game::Dice::EndReason::Stopped, Shared::Game::Dice::kNoSeat);
        return true;
    }

    bool DiceService::HasSeat(std::uint64_t playerNetworkId, std::uint32_t matchId, std::uint8_t slot) const {
        const auto found = _matches.find(matchId);
        return found != _matches.end() && slot < 2 && found->second.players[slot] == playerNetworkId;
    }

    std::uint32_t DiceService::MatchOf(std::uint64_t playerNetworkId) const {
        for (const auto &[matchId, match] : _matches) {
            if (match.players[0] == playerNetworkId || match.players[1] == playerNetworkId) {
                return matchId;
            }
        }
        return 0;
    }

    void DiceService::Update() {
        // A seat that never moves forfeits; collected first, because ending
        // tells scripts, and a handler may start or stop another match.
        const std::uint64_t now = Now();
        std::vector<std::pair<std::uint32_t, std::uint8_t>> expired;
        for (const auto &[matchId, match] : _matches) {
            if (now >= match.deadline) {
                expired.emplace_back(matchId, static_cast<std::uint8_t>(match.table.Turn() ^ 1));
            }
        }
        for (const auto &[matchId, winner] : expired) {
            Framework::Logging::GetLogger(kLogger)->info("Match {}: the seat to play went silent", matchId);
            End(matchId, Shared::Game::Dice::EndReason::TimedOut, winner);
        }
    }

    void DiceService::Forget(std::uint64_t playerNetworkId) {
        const std::uint32_t matchId = MatchOf(playerNetworkId);
        if (matchId == 0) {
            return;
        }
        const Match &match       = _matches.at(matchId);
        const std::uint8_t loser = match.players[0] == playerNetworkId ? 0 : 1;
        End(matchId, Shared::Game::Dice::EndReason::Left, static_cast<std::uint8_t>(loser ^ 1));
    }

    void DiceService::OnMove(const Shared::RPC::DiceMove &move, MafiaNet::Packet *packet) {
        if (!move.IsSane()) {
            return;
        }
        std::uint8_t seat = 0;
        Match *match      = FindBySender(packet, move.matchId, seat);
        if (match == nullptr) {
            return;
        }

        const auto kind = static_cast<Shared::Game::Dice::MoveKind>(move.kind);
        const bool due  = move.moveIndex == match->moves && seat == match->table.Turn() && !match->table.ThrowNeeded();
        const auto turnScore = match->table.TurnScore();
        const auto scored    = due ? match->table.Move(kind, move.heldMask) : std::nullopt;
        if (!scored.has_value()) {
            Framework::Logging::GetLogger(kLogger)->warn("Match {}: refused move {} from seat {} (kind {}, held {:#04x})", match->matchId, move.moveIndex, seat, move.kind, move.heldMask);
            Shared::RPC::DiceRefuse refuse;
            refuse.matchId                       = match->matchId;
            refuse.moveIndex                     = move.moveIndex;
            auto *net                            = Framework::CoreModules::GetNetworkPeer();
            Shared::Entities::HumanEntity *mover = Player::Players::Find(match->players[seat]);
            if (net != nullptr && mover != nullptr) {
                net->SendRPC(refuse, MafiaNet::ToGuid(mover->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
            return;
        }

        Shared::RPC::DiceCommit commit;
        commit.matchId   = match->matchId;
        commit.moveIndex = match->moves++;
        commit.seat      = seat;
        commit.kind      = move.kind;
        commit.heldMask  = move.heldMask;
        QueueProgress(*match, "diceMove", seat, kind == Shared::Game::Dice::MoveKind::Continue ? "continue" : "pass", *scored, move.heldMask, 0, turnScore + *scored);
        if (kind == Shared::Game::Dice::MoveKind::Pass) {
            QueueProgress(*match, "diceTurnEnd", seat, match->table.Over() ? "won" : "pass", turnScore + *scored, move.heldMask, 0, turnScore + *scored);
            match->turnOpen = false;
            if (!match->table.Over()) {
                ++match->turn;
                match->roll     = 0;
                match->turnOpen = true;
                QueueProgress(*match, "diceTurnStart", match->table.Turn());
            }
        }
        commit.deals     = DealPending(*match);
        commit.total0    = match->table.Total(0);
        commit.total1    = match->table.Total(1);
        match->deadline  = Now() + kTurnTimeoutMs;
        SendToSeats(*match, commit);
        PublishProgress(*match);

        if (match->table.Over()) {
            End(match->matchId, Shared::Game::Dice::EndReason::Won, match->table.Winner());
        }
        // A run of busts longer than one message carries: nothing either
        // client could play next, so nobody wins it.
        else if (match->table.ThrowNeeded()) {
            End(match->matchId, Shared::Game::Dice::EndReason::Failed, Shared::Game::Dice::kNoSeat);
        }
        FlushEvents();
    }

    void DiceService::OnLeave(const Shared::RPC::DiceLeave &leave, MafiaNet::Packet *packet) {
        if (!leave.IsSane()) {
            return;
        }
        std::uint8_t seat = 0;
        Match *match      = FindBySender(packet, leave.matchId, seat);
        if (match == nullptr) {
            return;
        }
        auto reason = static_cast<Shared::Game::Dice::EndReason>(leave.reason);
        // A client says only that it gave up or that its game failed; whether
        // anyone won otherwise is the server's to say.
        if (reason != Shared::Game::Dice::EndReason::Failed) {
            reason = Shared::Game::Dice::EndReason::GaveUp;
        }
        const std::uint8_t winner = reason == Shared::Game::Dice::EndReason::GaveUp ? static_cast<std::uint8_t>(seat ^ 1) : Shared::Game::Dice::kNoSeat;
        End(match->matchId, reason, winner);
    }

    std::vector<Shared::RPC::DiceDeal> DiceService::DealPending(Match &match) {
        std::vector<Shared::RPC::DiceDeal> deals;
        std::uniform_int_distribution<int> face(0, Shared::Game::Dice::kFaceCount - 1);
        while (match.table.ThrowNeeded() && deals.size() < Shared::RPC::kMaxDiceDeals) {
            Shared::RPC::DiceDeal deal;
            deal.seat = match.table.Turn();
            deal.faces.resize(match.table.DiceToThrow());
            for (std::uint8_t &value : deal.faces) {
                value = static_cast<std::uint8_t>(face(_random));
            }
            const auto mask  = match.table.FreeMask();
            const auto score = match.table.TurnScore();
            const bool bust  = match.table.Throw(deal.faces);
            ++match.roll;
            QueueProgress(match, "diceRoll", deal.seat, bust ? "bust" : "rolled", 0, 0, mask, score);
            if (bust) {
                QueueProgress(match, "diceTurnEnd", deal.seat, "bust", 0, 0, 0, score);
                ++match.turn;
                match.roll = 0;
                QueueProgress(match, "diceTurnStart", match.table.Turn());
            }
            deals.push_back(std::move(deal));
        }
        return deals;
    }

    void DiceService::End(std::uint32_t matchId, Shared::Game::Dice::EndReason reason, std::uint8_t winnerSeat) {
        auto found = _matches.find(matchId);
        if (found == _matches.end()) {
            return;
        }
        // Forgotten before anyone hears of it, so a handler starting a new
        // match for the same players finds them free.
        Match match = std::move(found->second);
        _matches.erase(found);

        Shared::RPC::DiceMatchEnd end;
        end.matchId    = matchId;
        end.reason     = static_cast<std::uint8_t>(reason);
        end.winnerSeat = winnerSeat;
        end.total0     = match.table.Total(0);
        end.total1     = match.table.Total(1);
        SendToSeats(match, end);

        Framework::Logging::GetLogger(kLogger)->info("Match {} over ({}): seat {} won, {} : {}", matchId, ReasonName(reason), winnerSeat, end.total0, end.total1);
        if (match.turnOpen) {
            QueueProgress(match, "diceTurnEnd", match.table.Turn(), ReasonName(reason), 0, 0, 0, match.table.TurnScore());
        }
        PublishProgress(match);
        const auto players = match.players;
        _events.push_back([=] {
            Scripting::Events::DiceMatchEnd(matchId, players[0], players[1], winnerSeat, ReasonName(reason), end.total0, end.total1);
        });
        FlushEvents();
    }

    void DiceService::QueueProgress(Match &match, const char *event, std::uint8_t seat, const char *reason, std::uint32_t points, std::uint8_t heldMask, std::uint8_t rolledMask, std::uint32_t turnScore) {
        nlohmann::json faces = nlohmann::json::array();
        if (rolledMask != 0) {
            for (std::size_t i = 0; i < Shared::Game::Dice::kDiceCount; ++i) {
                faces.push_back(match.table.Face(i) + 1);
            }
        }
        match.progress.emplace_back(event, nlohmann::json {{"match", match.matchId}, {"player", match.players[seat]}, {"seat", seat}, {"turn", match.turn}, {"roll", match.roll}, {"moveIndex", match.moves}, {"turnScore", turnScore}, {"totalScore", match.table.Total(seat)},
                                               {"faces", faces}, {"rolledMask", rolledMask}, {"heldMask", heldMask}, {"points", points}, {"reason", reason}});
    }

    void DiceService::PublishProgress(Match &match) {
        for (auto &entry : match.progress) {
            _events.push_back([event = std::move(entry.first), data = std::move(entry.second)] {
                Scripting::Events::DiceProgress(event.c_str(), data);
            });
        }
        match.progress.clear();
    }

    void DiceService::FlushEvents() {
        // A handler can stop this match or start another. Queue immutable
        // snapshots and never keep a match reference across a script callback.
        if (_emitting) {
            return;
        }
        _emitting = true;
        while (!_events.empty()) {
            auto next = std::move(_events.front());
            _events.pop_front();
            next();
        }
        _emitting = false;
    }

    DiceService::Match *DiceService::FindBySender(MafiaNet::Packet *packet, std::uint32_t matchId, std::uint8_t &seat) {
        Shared::Entities::HumanEntity *sender = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        auto found                            = _matches.find(matchId);
        if (sender == nullptr || found == _matches.end()) {
            return nullptr;
        }
        Match &match = found->second;
        if (match.players[0] == sender->GetNetworkID()) {
            seat = 0;
            return &match;
        }
        if (match.players[1] == sender->GetNetworkID()) {
            seat = 1;
            return &match;
        }
        return nullptr;
    }

    template <typename Payload>
    void DiceService::SendToSeats(const Match &match, Payload &payload) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        for (const std::uint64_t playerId : match.players) {
            if (Shared::Entities::HumanEntity *player = Player::Players::Find(playerId)) {
                net->SendRPC(payload, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
    }
} // namespace KCDC::Features::Dice
