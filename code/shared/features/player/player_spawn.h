#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * Owner -> server: the level is up and the body has nowhere to stand yet.
     *
     * A pull, because the server learns of a connection from the identity
     * message -- long before that client has a level to apply a placement to.
     */
    struct RequestSpawn {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::RequestSpawn");

        std::uint64_t networkId = 0;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, networkId);
        }

        bool IsSane() const {
            return valid && networkId != 0;
        }
    };

    /**
     * Where the server wants one arriving player to stand.
     *
     * Sent only to the player being placed -- the owner is authoritative for
     * its own pose, so a server-side write would be gone by its next capture.
     */
    struct SpawnPlayer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SpawnPlayer");

        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;

        float rotationX = 0.0F;
        float rotationY = 0.0F;
        float rotationZ = 0.0F;
        float rotationW = 1.0F;

        /** False is a real answer: nobody chose, so the body keeps what the level gave it. */
        bool placed = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, x) && bs->Serialize(write, y) && bs->Serialize(write, z) && bs->Serialize(write, rotationX) && bs->Serialize(write, rotationY) && bs->Serialize(write, rotationZ) && bs->Serialize(write, rotationW)
                 && bs->Serialize(write, placed);
        }

        bool IsSane() const {
            if (!valid) {
                return false;
            }
            if (!placed) {
                return true;
            }
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(rotationX) || !std::isfinite(rotationY) || !std::isfinite(rotationZ) || !std::isfinite(rotationW)) {
                return false;
            }
            // A zero quaternion has no facing to normalize into one.
            const double lengthSquared = static_cast<double>(rotationX) * rotationX + static_cast<double>(rotationY) * rotationY + static_cast<double>(rotationZ) * rotationZ + static_cast<double>(rotationW) * rotationW;
            return lengthSquared >= 1e-6;
        }
    };
} // namespace KCDC::Shared::RPC
