#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    // Server to one player. New connections start without builder access.
    struct SetWorldBuilderAccess {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetWorldBuilderAccess");

        bool enabled = false;
        bool valid   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, enabled);
        }

        bool IsSane() const {
            return valid;
        }
    };
} // namespace KCDC::Shared::RPC
