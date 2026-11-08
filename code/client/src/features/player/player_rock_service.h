#pragma once

#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_projectile.h"
#include "shared/features/player/player_rock.h"

#include <cstdint>

namespace KCDC::Features::Player {
    // Thrown pebbles. The local player's releases are reported once; a
    // streamed player's release is launched here as the same native decoy,
    // whose flight, contact and noise the game then plays on its own.
    class PlayerRockService final {
      public:
        static PlayerRockService &Get();
        static void Install();

        /** Accepted native release; reports only the local player's decoys. Game thread. */
        void ProjectileLaunched(SDK::wh::entitymodule::CProjectile *projectile);

      private:
        void Replay(const KCDC::Shared::RPC::RockLaunched &message);

        std::uint64_t _nextSequence = 1;
    };
} // namespace KCDC::Features::Player
