#pragma once

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>
#include <string>

namespace KCDC::Shared::RPC {
    /** One validated immutable definition, or a connection reset marker. */
    struct CustomBuffDefinition {
        static constexpr auto kIdentifier     = FW_RPC_IDENTIFIER("KCDC::CustomBuffDefinition");
        static constexpr std::uint32_t kMaximumBytes = 16384;
        std::string body;
        bool valid = true;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            std::uint32_t size = write && body.size() <= kMaximumBytes ? static_cast<std::uint32_t>(body.size()) : 0;
            valid              = (!write || body.size() <= kMaximumBytes) && stream->Serialize(write, size) && size <= kMaximumBytes;
            if (valid && !write && static_cast<std::uint64_t>(stream->GetNumberOfUnreadBits()) < static_cast<std::uint64_t>(size) * 8) {
                valid = false;
            }
            if (!valid) {
                return;
            }
            if (!write) {
                body.resize(size);
            }
            valid = stream->Serialize(write, body.data(), size);
        }
    };
} // namespace KCDC::Shared::RPC
