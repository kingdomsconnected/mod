#pragma once

#include "features/inventory/inventory_gameplay.h"
#include "shared/features/pickpocket/pickpocket_rpc.h"
#include "shared/game/item_classes.h"

#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Pickpocket {
    /** One stolen line, as the `playerPickpocketed` event reports it. */
    struct StolenLine {
        const Shared::Game::ItemClassEntry *item = nullptr;
        std::uint32_t amount                     = 0;
    };

    /**
     * Server authority for one player picking another's pocket.
     *
     * The thief plays the game's own minigame against the victim's body on
     * their machine, and that machine decides what it always decided: whether
     * the victim noticed, from the victim body's facing and both souls'
     * stats. What the victim carries and what leaves them is the server's:
     *
     * 1. The thief's charge begins and a request arrives. The gamemode may
     *    refuse it (`playerPickpocketStart`); a victim already being robbed
     *    or out of reach is refused here.
     * 2. The thief is handed the victim's unworn items, one line per class,
     *    and its game builds the loot grid from them.
     * 3. The thief reports how it ended. A claim within what was offered moves
     *    those units from the victim's inventory to the thief's, properties
     *    and all; one the victim can no longer cover moves nothing.
     *
     * One attempt per thief and one per victim at a time.
     */
    class PickpocketService final {
      public:
        static PickpocketService &Get();

        /** Registers the RPC handlers. Idempotent. */
        void Install();

        /** Server tick: ends attempts whose thief went quiet. */
        void Update();

        /** Drops every attempt a leaving player is part of. */
        void Forget(std::uint64_t playerNetworkId);

      private:
        struct Attempt {
            std::uint32_t attemptId = 0;
            std::uint32_t requestId = 0;
            std::uint64_t thiefId   = 0;
            std::uint64_t victimId  = 0;

            /** What the thief was handed. */
            std::vector<KCDC::Features::Inventory::PocketLine> offer;

            /** When the offer went out: a theft cannot settle before the charge could fill. */
            std::uint64_t offeredAt = 0;

            /** When an attempt nobody reports on gives up. */
            std::uint64_t deadline = 0;
        };

        void OnRequest(const Shared::RPC::PickpocketRequest &request, MafiaNet::Packet *packet);
        void OnResult(const Shared::RPC::PickpocketResult &result, MafiaNet::Packet *packet);

        /** Moves a claim from victim to thief and tells scripts what moved. */
        static void Settle(const Attempt &attempt, const std::vector<Shared::RPC::PickpocketItem> &claim, std::uint64_t now);

        void SendVerdict(std::uint64_t thiefId, std::uint32_t requestId, Shared::RPC::PickpocketRefusal refusal, const std::vector<Shared::RPC::PickpocketItem> &items) const;

        Attempt *FindByThief(std::uint64_t thiefId, std::uint32_t requestId);
        bool IsVictim(std::uint64_t playerId) const;

        std::unordered_map<std::uint32_t, Attempt> _attempts;

        /** When each thief last asked, keyed by NetworkID; see kMinRequestIntervalMs. */
        std::unordered_map<std::uint64_t, std::uint64_t> _lastRequestAt;
        std::uint32_t _nextAttempt = 1;
        bool _installed            = false;
    };
} // namespace KCDC::Features::Pickpocket
