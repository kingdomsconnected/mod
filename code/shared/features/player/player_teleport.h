#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstddef>
#include <string>

namespace KCDC::Shared::RPC {
    /** A peer must never choose how much a receiver stores. */
    inline constexpr std::size_t kMaxTeleportLabel = 64;

    /**
     * Where the server wants one player to stand.
     *
     * Sent only to the player being moved -- the owner is authoritative for
     * its own pose, so a server-side write would be gone by its next capture.
     */
    struct TeleportPlayer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::TeleportPlayer");

        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;

        /** Display only; the client echoes it back in its teleport panel. */
        std::string label;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, x) && bs->Serialize(write, y) && bs->Serialize(write, z) && bs->Serialize(write, label);
        }

        bool IsSane() const {
            return valid && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && label.size() <= kMaxTeleportLabel;
        }
    };
} // namespace KCDC::Shared::RPC
