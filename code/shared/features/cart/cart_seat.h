#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A client asking to be put in a cart, or to be let out of one.
     *
     * The game's own prompt raises it: a player who uses a wagon's mount
     * point is not seated by their own game, their client asks, and the seat
     * the server writes back is what their game then climbs into. The player
     * is the sending connection's own, never named here, so a client can only
     * ever move itself.
     */
    struct CartSeatRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CartSeatRequest");

        /** What the request asks for. */
        enum class Kind : std::uint8_t {
            Front    = 0, // the bench prompt: the reins
            Back     = 1, // the rail prompt: the back
            Leave    = 2, // out, wherever the player sits
            Boarding = 3, // the player's own climb in has started
            Seated   = 4, // ... and has ended
            Leaving  = 5, // the player's own climb down has started
        };

        std::uint64_t cartNetworkId = 0;
        std::uint8_t kind           = 0;

        /** Boarding, Seated and Leaving: the seat the climb is in. */
        std::uint8_t seat = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, cartNetworkId);
            bs->Serialize(write, kind);
            bs->Serialize(write, seat);
        }

        bool IsSane() const {
            return kind <= static_cast<std::uint8_t>(Kind::Leaving);
        }
    };
} // namespace KCDC::Shared::RPC
