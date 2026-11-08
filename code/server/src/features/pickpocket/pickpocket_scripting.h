#pragma once

#include "features/pickpocket/pickpocket_service.h"

#include <cstdint>
#include <v8.h>
#include <vector>

namespace KCDC::Scripting {
    /**
     * Pickpocketing between players has no global of its own: it is played on
     * the game's own Rob entry, and what the gamemode decides -- who may rob
     * whom, what being caught or robbed costs -- it decides through the three
     * events registered here.
     */
    void RegisterPickpocketBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What this feature tells loaded resources happened between two players.
    namespace Events {
        /** A player began picking another's pocket. False when a handler refused it. */
        bool PlayerPickpocketStart(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId);

        /** A theft settled; `lines` is what really moved, which may be less than was taken, or nothing. */
        void PlayerPickpocketed(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId, const std::vector<Features::Pickpocket::StolenLine> &lines);

        /** The victim noticed the thief. */
        void PlayerPickpocketCaught(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
