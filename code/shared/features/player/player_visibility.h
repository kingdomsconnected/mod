#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /**
     * Server -> owner: whether this player's body is drawn at all.
     *
     * The owner is authoritative for its own body, so this is a request it
     * applies and republishes as `HumanEntity::bodyHidden`, exactly as a
     * disguise or an appearance is -- one peer stays the source of what
     * everyone draws of that body.
     *
     * Hidden is a look, not a state of being: the body still stands where it
     * stood, still collides, still fights and still dies, and the NPCs around
     * it never stopped seeing it.
     */
    struct SetPlayerVisible {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetPlayerVisible");

        bool visible = true;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, visible);
        }

        bool IsSane() const {
            return valid;
        }
    };
} // namespace KCDC::Shared::RPC
