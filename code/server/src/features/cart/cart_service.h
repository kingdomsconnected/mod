#pragma once

#include <mafianet/get_time.h>
#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace KCDC::Features::Cart {
    /**
     * Server-side authority over who sits in which cart: the seat RPC, and
     * what it has to remember between two messages.
     */
    class CartService final {
      public:
        static CartService &Get();

        /** Registers the seat RPC. Call once at startup. */
        void Install();

        /** Forgets a peer's rate limits, when it disconnects. */
        void Forget(MafiaNet::PeerGuid guid);

        /**
         * Marks a cart as being destroyed, false when it already is: a script
         * handler raised by the destroy that destroys it again, or seats
         * somebody in it, is answered as if it were gone.
         */
        bool BeginDestroy(std::uint64_t cartNetworkId);
        void EndDestroy(std::uint64_t cartNetworkId);
        bool IsBeingDestroyed(std::uint64_t cartNetworkId) const;

      private:
        /** Whether `guid` may be answered again, taking the turn if so. */
        static bool TakeTurn(std::unordered_map<MafiaNet::PeerGuid, MafiaNet::Time> &turns, MafiaNet::PeerGuid guid, MafiaNet::Time interval);

        // Per peer: when a seat request may next be answered. A driver's seat
        // runs a pin and script events, which cost the server far more than
        // the few bytes asking for it.
        std::unordered_map<MafiaNet::PeerGuid, MafiaNet::Time> _nextSeatRequest;

        std::unordered_set<std::uint64_t> _destroying;
    };
} // namespace KCDC::Features::Cart
