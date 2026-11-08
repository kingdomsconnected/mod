#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * The server telling one client what its player does with a saddle.
     *
     * Sent to the player it is about and nobody else, because only that
     * client's game can move its own body in or out of a saddle; everyone
     * else follows the horse's replicated rider.
     *
     * Getting off covers three cases that end the same way. Two players
     * mounting inside one tick both succeed locally and the server picks one;
     * a `horseMounting` handler refused the ride; or a script took the player
     * out of the saddle. In each the server's record already says the saddle
     * is empty, so the client gets off without reporting a dismount.
     */
    struct HorseRideOrder {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseRideOrder");

        std::uint64_t horseNetworkId = 0;

        /** True to climb into this horse's saddle, false to get out of it. */
        bool mount = false;

        /** On a mount, straight into the saddle rather than the get-on. */
        bool instant = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, horseNetworkId);
            bs->Serialize(write, mount);
            bs->Serialize(write, instant);
        }
    };
} // namespace KCDC::Shared::RPC
