#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A client reporting that its player got into or out of a saddle.
     *
     * Edge-triggered: the continuous answer to "who is riding what" is the
     * horse's replicated rider field, and this is what moves it. Only the
     * horse is taken from the message -- the rider is stamped from the sending
     * connection's own avatar, so a client can only ever seat itself.
     *
     * Mounting hands the horse's authority to the rider's client, which from
     * that moment is the only peer simulating the animal.
     */
    struct HorseMount {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseMount");

        /** The horse's NetworkID. On a dismount this is the horse being left. */
        std::uint64_t horseNetworkId = 0;

        /** True on getting in, false on getting out. */
        bool mounted = false;

        /**
         * On getting in, whether the rider's game seated it straight away --
         * a server order that asked for it -- rather than playing the get-on.
         */
        bool instant = false;

        /**
         * On getting out, the flank the rider's game chose for the get-off,
         * 1 or 2 as `StopRide` takes it, so every copy steps off the same
         * side. 0 when it was not a get-off of the rider's own.
         */
        std::uint8_t dismountSide = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, horseNetworkId);
            bs->Serialize(write, mounted);
            bs->Serialize(write, instant);
            bs->Serialize(write, dismountSide);
        }
    };
} // namespace KCDC::Shared::RPC
