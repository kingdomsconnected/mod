#pragma once

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /** What a player asked of a siege ladder: to push a standing one off the wall, or to raise a fallen one against it. */
    enum class SiegeLadderAction : std::uint8_t { Push, Raise };

    inline constexpr std::uint8_t kLastSiegeLadderAction = static_cast<std::uint8_t>(SiegeLadderAction::Raise);

    /**
     * Client -> server: the local player held the push prompt at a ladder's
     * top, or pressed the raise prompt at a fallen one's foot.
     *
     * Carries what was asked, so a push that reaches the server after
     * somebody else's has nothing left to push. The server checks the player
     * stands within reach of the end the action is done from.
     */
    struct SiegeLadderUse {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeLadderUse");

        std::uint64_t ladderNetworkId = 0;
        SiegeLadderAction action      = SiegeLadderAction::Push;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(ladderNetworkId);
            std::uint8_t wireAction = static_cast<std::uint8_t>(action);
            fields.Field(wireAction);
            _validAction = wireAction <= kLastSiegeLadderAction;
            action       = _validAction ? static_cast<SiegeLadderAction>(wireAction) : SiegeLadderAction::Push;
            _complete    = fields.Good();
        }

        bool IsSane() const {
            return _complete && _validAction && ladderNetworkId != 0;
        }

      private:
        bool _complete    = true;
        bool _validAction = true;
    };
} // namespace KCDC::Shared::RPC
