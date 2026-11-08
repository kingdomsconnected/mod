#pragma once

#include "shared/game/cook_pot_catalog.h"
#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    struct CookPotContext {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CookPotContext");
        std::uint64_t revision                   = 0;
        std::uint32_t world                      = 0;
        std::uint32_t incarnation                = 0;
        bool valid                               = true;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            valid = stream->Serialize(write, revision) && stream->Serialize(write, world) && stream->Serialize(write, incarnation);
        }
    };

    struct CookPotRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CookPotRequest");
        std::uint64_t sequence                   = 0;
        std::uint64_t guid                       = 0;
        std::uint64_t contextRevision            = 0;
        std::uint32_t world                      = 0;
        std::uint32_t incarnation                = 0;
        bool completion                          = false;
        bool eaten                               = false;
        bool valid                               = true;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            valid = stream->Serialize(write, sequence) && stream->Serialize(write, guid) && stream->Serialize(write, contextRevision) && stream->Serialize(write, world) && stream->Serialize(write, incarnation) && stream->Serialize(write, completion)
                    && stream->Serialize(write, eaten);
        }
    };

    struct CookPotReply {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CookPotReply");
        std::uint64_t sequence                   = 0;
        std::uint64_t guid                       = 0;
        KCDC::Shared::Game::CookPotFood food     = KCDC::Shared::Game::CookPotFood::Goulash;
        bool granted                             = false;
        bool nativeEffects                       = true;
        bool valid                               = true;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            valid = stream->Serialize(write, sequence) && stream->Serialize(write, guid) && stream->Serialize(write, granted) && stream->Serialize(write, nativeEffects) && stream->Serialize(write, food) && !KCDC::Shared::Game::CookPotFoodName(food).empty();
        }
    };
} // namespace KCDC::Shared::RPC
