#pragma once

#include "shared/features/player/player_rock.h"
#include "shared/features/player/player_rock_throw_history.h"

#include <mafianet/types.h>
#include <unordered_map>

namespace KCDC::Features::Player {
    // Validates and relays the pebbles a player throws; every client then
    // launches its own native decoy.
    class Rock final {
      public:
        void Install();
        void Update();

      private:
        void OnLaunched(const KCDC::Shared::RPC::RockLaunched &message, MafiaNet::Packet *packet);

        // Per thrower body; Update drops a body once it is gone, which
        // covers disconnection.
        std::unordered_map<std::uint64_t, KCDC::Shared::Game::RockThrowHistory> _players;
    };
} // namespace KCDC::Features::Player
