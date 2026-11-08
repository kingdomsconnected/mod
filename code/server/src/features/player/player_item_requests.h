#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_take_item.h"
#include "shared/game/item_classes.h"

#include <mafianet/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Takes items back out of players' inventories, and waits to hear what went.
     *
     * `Players::GiveItem` needs none of this: an inventory always accepts, so
     * the packet goes out and that is the end of it. A take can come back short,
     * and only the owning client knows by how much, so this is the book of who
     * was asked what and has not answered yet.
     *
     * A handler here backs a promise a resource is holding, so as in
     * `WorldQueryService` every one runs exactly once -- on the answer, on the
     * timeout, or on the player leaving.
     */
    class ItemRequests final {
      public:
        /** How much came out, or why nothing did. */
        struct Outcome {
            /** False when the client never answered, or had no inventory to look in. */
            bool answered = false;

            /** Empty when the client answered. Otherwise what to tell the script. */
            std::string reason;

            /** Units the client actually removed, never more than were asked for. */
            std::uint32_t removed = 0;

            /** Units the request asked for, echoed back so a caller can compare. */
            std::uint32_t requested = 0;
        };

        /** Runs on the server tick, once per request. */
        using OutcomeHandler = std::function<void(const Outcome &)>;

        void Install();
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * Asks one player's client to remove `amount` units of `item`.
         *
         * False for an amount the wire refuses or a missing peer, and then
         * `handler` never runs and the caller answers for itself. A client that
         * simply goes quiet is not a false: it settles through the timeout.
         */
        bool Take(Shared::Entities::HumanEntity &player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount, OutcomeHandler handler);

        std::size_t PendingCount() const {
            return _pending.size();
        }

      private:
        struct Pending {
            MafiaNet::PeerGuid sender = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t expiresAt   = 0;
            std::uint32_t requested   = 0;
            OutcomeHandler handler;
        };

        void QueueResult(const Shared::RPC::TakeItemResult &result, MafiaNet::Packet *packet);
        void ResolveResult(const Shared::RPC::TakeItemResult &result, MafiaNet::PeerGuid sender);

        /** Drops the request as it settles, so its handler cannot run twice. */
        void Settle(std::uint64_t requestId, Outcome outcome);

        std::mutex _incomingMutex;
        std::vector<std::pair<Shared::RPC::TakeItemResult, MafiaNet::PeerGuid>> _incomingResults;
        std::unordered_map<std::uint64_t, Pending> _pending;
        std::uint64_t _nextRequestId = 1;
    };
} // namespace KCDC::Features::Player
