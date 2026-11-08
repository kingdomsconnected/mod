#pragma once

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::RPC {
    /**
     * Client -> server: the local player crossed a marker's boundary.
     *
     * A transition rather than a state: the engine's area raises enter and
     * leave, and occupancy every tick would cost a message per marker per
     * player to say nothing changed.
     */
    struct MarkerCrossed {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::MarkerCrossed");

        /** The marker whose boundary was crossed. */
        std::uint64_t markerNetworkId = 0;

        /** True on the way in, false on the way out. */
        bool entered = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(markerNetworkId);
            fields.Field(entered);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && markerNetworkId != 0;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
