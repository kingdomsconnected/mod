#pragma once

#include "shared/features/inventory/inventory_message.h"

namespace KCDC::Shared::RPC {
    /** One native intent as a JSON body; never inventory, character or result state. */
    struct AlchemyRequest {
        static constexpr auto kIdentifier     = FW_RPC_IDENTIFIER("KCDC::AlchemyRequest");
        static constexpr std::uint32_t kMaximumBytes = 8192;
        std::string body;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            std::uint32_t size = write ? static_cast<std::uint32_t>(body.size()) : 0;
            valid              = (!write || (!body.empty() && body.size() <= kMaximumBytes)) && stream->Serialize(write, size) && size != 0 && size <= kMaximumBytes;
            if (!valid) {
                return;
            }
            if (!write) {
                body.resize(size);
            }
            valid = stream->Serialize(write, body.data(), size);
        }
    };

    /** The server's answer, or a push when `request` is empty. */
    struct AlchemyResponse: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::AlchemyResponse");
    };
} // namespace KCDC::Shared::RPC
