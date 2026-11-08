#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /**
     * The pace rule the server puts on one player.
     *
     * Sent only to the player it is about, and only the rule: the pace
     * everyone else draws already arrives as `HumanEntity::moveSpeedTag`,
     * published by the body's own owner. The whole rule travels every time,
     * because the server keeps no copy of what a client is currently under.
     */
    struct SetPlayerMovementMode {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetPlayerMovementMode");

        /** A body that spawns starts walking; the player's toggle still works. */
        bool walkByDefault = false;

        /** The body may not run or sprint at all, toggle or no toggle. */
        bool walkEnforced = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, walkByDefault) && bs->Serialize(write, walkEnforced);
        }

        bool IsSane() const {
            return valid;
        }
    };
} // namespace KCDC::Shared::RPC
