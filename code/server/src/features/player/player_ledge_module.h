#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_ledge.h"

#include <cstddef>

namespace KCDC::Features::Player {
    /** Relay for one player's vault, stamped with who went over. */
    class Ledge final {
      public:
        static std::size_t Relay(const KCDC::Shared::RPC::PlayerLedgeGrab &grab, KCDC::Shared::Entities::HumanEntity &actor);
    };
} // namespace KCDC::Features::Player
