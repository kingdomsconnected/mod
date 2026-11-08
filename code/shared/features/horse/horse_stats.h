#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A client telling the server what the engine says about a horse.
     *
     * A horse's carrying capacity is computed from its soul, its buffs and its
     * saddlebags on every read, so only a peer running the game can answer it.
     *
     * Sent only by the horse's simulator, and taken only from it: a copy that
     * has just streamed in reads the bare animal until it is dressed. A
     * client sends only when what it reads differs from both the replicated
     * value and what it last sent, so the report falls silent next tick.
     */
    struct HorseStats {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseStats");

        std::uint64_t horseNetworkId = 0;

        /** The horse's inventory capacity, in the engine's own weight units. */
        float inventoryCapacity = 0.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, horseNetworkId);
            bs->Serialize(write, inventoryCapacity);
        }
    };
} // namespace KCDC::Shared::RPC
