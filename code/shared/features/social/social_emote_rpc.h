#pragma once

#include "shared/game/emote_catalog.h"

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    struct Emote {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::Emote");
        enum Phase : std::uint8_t {
            Start,
            Stop
        };

        std::uint64_t actorNetworkId = 0;
        std::uint64_t sequence       = 0;
        std::uint16_t emoteId        = 0;
        std::uint8_t phase           = Start;

        bool IsSane() const {
            return _complete && sequence != 0 && phase <= Stop && Game::FindEmote(emoteId) != nullptr;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = bs->Serialize(write, actorNetworkId);
            _complete = bs->Serialize(write, sequence) && _complete;
            _complete = bs->Serialize(write, emoteId) && _complete;
            _complete = bs->Serialize(write, phase) && _complete;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
