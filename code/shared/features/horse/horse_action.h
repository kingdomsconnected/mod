#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * One thing done to a horse, broadcast for every client to perform -- or,
     * for one that changes the animal's life, sent to the client running it
     * alone, whose soul the other copies follow.
     *
     * These are the game's own actions rather than poses, so each client hands
     * the request to its own copy and the engine plays it out: what everybody
     * ends up watching is the real animation, not a synchronised approximation
     * of one. They are relayed rather than replicated because they are events
     * -- a rear that happened is not a state anybody can hold.
     */
    struct HorseAction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseAction");

        enum Type : std::uint8_t {
            /** The horse rears and throws whoever is on it off. */
            RearAndThrowDown = 0,

            /** `attackerNetworkId` drags this horse's rider out of the saddle. */
            PullDownRider = 1,

            /** The client running the horse ends its knockout, the way the game ends one early. Sent to that client alone. */
            Wake = 2,
        };

        std::uint8_t type            = RearAndThrowDown;
        std::uint64_t horseNetworkId = 0;

        /** The acting player, for the actions that have one. */
        std::uint64_t attackerNetworkId = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, type);
            bs->Serialize(write, horseNetworkId);
            bs->Serialize(write, attackerNetworkId);
        }
    };
} // namespace KCDC::Shared::RPC
