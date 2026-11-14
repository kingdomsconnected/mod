#pragma once

#include "shared/features/dice/dice_rpc.h"
#include "shared/features/dice/dice_rules.h"

#include <mafianet/types.h>

#include <array>
#include <deque>
#include <functional>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Dice {
    /**
     * Server authority over dice matches between two players.
     *
     * Each player plays the game's own dice table on their machine, the other
     * player's body sitting opposite. What the dice show is decided here and
     * nowhere else: the server deals every throw, checks every move against
     * the rules the engine plays (`Shared::Game::Dice`), and keeps the score.
     *
     * A throw is dealt only once the move that leads to it is committed, so no
     * client ever holds the faces of a throw before the decision that makes
     * it -- the press that asks for it is held on the client until the commit
     * comes back carrying them.
     *
     * One match per player at a time. What a match is played for is the
     * gamemode's, through `diceMatchEnd`.
     *
     * The second seat may be a server NPC instead of a player. Nothing plays
     * for it here: the deals and the rules are the same, its moves come from
     * a script through `Move`, and the one player at the table is the only
     * machine told anything. That client seats the NPC's body as the game
     * would seat any opponent.
     */
    class DiceService final {
      public:
        static DiceService &Get();

        /** Registers the RPC handlers. Idempotent. */
        void Install();

        /** Server tick: `Sweep` at the current time. */
        void Update();

        /**
         * Ends the matches whose seat to play has been silent until `now`, and
         * those whose NPC seat is gone or dead. Apart from `Update` so a test
         * can state the time.
         */
        void Sweep(std::uint64_t now);

        /** A leaving player loses the match they were in. */
        void Forget(std::uint64_t playerNetworkId);

        /**
         * Starts a match between two bodies standing at a dice table: a
         * player on the first seat, and a player or an NPC on the second.
         * Which of them throws first is drawn. 0 with `error` set when it
         * cannot start.
         */
        std::uint32_t Start(std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint32_t targetScore, std::string &error, const std::array<std::string, 2> &names = {});
        bool SetNames(std::uint32_t matchId, const std::array<std::string, 2> &names);

        /**
         * Plays the NPC seat's move, by the rules a player's move is checked
         * against. False, with nothing changed, when the match has no NPC
         * seat, it is not that seat's turn, or the move is not legal.
         */
        bool Move(std::uint32_t matchId, std::uint8_t heldMask, bool pass);

        /** Ends a match from a script. False when it had already ended. */
        bool Stop(std::uint32_t matchId);

        /** The match a player or an NPC is in, or 0. */
        std::uint32_t MatchOf(std::uint64_t playerNetworkId) const;
        bool HasSeat(std::uint64_t playerNetworkId, std::uint32_t matchId, std::uint8_t slot) const;

      private:
        struct Match {
            std::uint32_t matchId = 0;
            std::array<std::uint64_t, 2> players {};

            /** The seat an NPC holds, moved by `Move` and never by a client; `kNoSeat` between two players. */
            std::uint8_t scriptSeat = Shared::Game::Dice::kNoSeat;
            Shared::Game::Dice::Table table;

            /** Moves committed so far; the index the next move must carry. */
            std::uint32_t moves = 0;
            std::uint32_t turn  = 1;
            std::uint32_t roll  = 0;
            bool turnOpen       = true;
            std::vector<std::pair<std::string, nlohmann::json>> progress;

            /** When the seat to play forfeits by silence. */
            std::uint64_t deadline = 0;

            Match(std::uint32_t targetScore, std::uint8_t firstSeat): table(targetScore, firstSeat) {}
        };

        void QueueProgress(Match &match, const char *event, std::uint8_t seat, const char *reason = "", std::uint32_t points = 0, std::uint8_t heldMask = 0, std::uint8_t rolledMask = 0, std::uint32_t turnScore = 0);
        void PublishProgress(Match &match);
        void FlushEvents();
        std::deque<std::function<void()>> _events;
        bool _emitting = false;
        void OnMove(const Shared::RPC::DiceMove &move, MafiaNet::Packet *packet);
        void OnLeave(const Shared::RPC::DiceLeave &leave, MafiaNet::Packet *packet);

        /**
         * Applies a move of `seat` when it is that seat's to make and legal,
         * deals what follows, and tells the players and the scripts. False
         * with nothing changed otherwise. The match may be over, and `match`
         * gone, once this returns true.
         */
        bool Commit(Match &match, std::uint8_t seat, Shared::Game::Dice::MoveKind kind, std::uint8_t heldMask);

        /** Deals every throw the table needs before the next decision, applying each. */
        std::vector<Shared::RPC::DiceDeal> DealPending(Match &match);

        /** Tells the players, forgets the match, then tells scripts. */
        void End(std::uint32_t matchId, Shared::Game::Dice::EndReason reason, std::uint8_t winnerSeat);

        Match *FindBySender(MafiaNet::Packet *packet, std::uint32_t matchId, std::uint8_t &seat);

        template <typename Payload>
        void SendToSeats(const Match &match, Payload &payload) const;

        std::unordered_map<std::uint32_t, Match> _matches;
        std::uint32_t _nextMatch = 1;
        std::mt19937 _random {std::random_device {}()};
        bool _installed = false;
    };
} // namespace KCDC::Features::Dice
