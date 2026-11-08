#pragma once

#include <mafianet/get_time.h>

#include <cstdint>

namespace KCDC::ServerClock {
    /** The server clock in milliseconds: what a replicated step start or launch is stamped with, and what clients play it out against. */
    inline std::uint64_t NowMs() {
        return MafiaNet::GetTimeUS() / 1000;
    }
} // namespace KCDC::ServerClock
