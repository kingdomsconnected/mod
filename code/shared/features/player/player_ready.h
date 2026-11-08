#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /**
     * Owner -> server: this player can play -- the game has taken its own
     * loading screen down for the level this session loaded.
     *
     * Carries nothing: the server knows the sender's body from the
     * connection, and a body named in the message would only be something
     * else to check.
     */
    struct PlayerReady {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerReady");

        void Serialize(MafiaNet::BitStream *, bool) {}
    };
} // namespace KCDC::Shared::RPC
