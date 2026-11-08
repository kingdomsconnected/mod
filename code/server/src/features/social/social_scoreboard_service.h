#pragma once

#include "shared/features/social/social_scoreboard_rpc.h"

#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>

namespace KCDC::Features::Social {
    /**
     * Answers the scoreboard: who is connected, and how far away each of them
     * is on the wire.
     *
     * Nothing is pushed. A client asks when its session starts and then about
     * every two seconds while its board is up, so the roster costs nothing
     * while nobody is looking at it. `_revision` moves on every join and
     * leave; an ask that names the current one is answered with pings alone.
     *
     * On unless the `mod` object sets `scoreboard` to false. A disabled
     * server answers no ask at all, so a client that asks anyway learns
     * nothing.
     */
    class ScoreboardService final {
      public:
        void Install(bool enabled);

        /** A connection became a player or stopped being one: the roster is a different set. */
        void OnRosterChanged();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

      private:
        void Answer(const KCDC::Shared::RPC::ScoreboardRequest &request, MafiaNet::PeerGuid guid);

        bool _enabled = true;

        /** Starts above zero, because zero is what a client that holds nothing asks with. */
        std::uint32_t _revision = 1;

        /** When each connection was last answered, so a client asking every frame is answered a few times a second. */
        std::unordered_map<MafiaNet::PeerGuid, std::uint64_t> _answeredAt;
    };
} // namespace KCDC::Features::Social
