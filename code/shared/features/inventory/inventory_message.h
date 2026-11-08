#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    /** Bounded JSON envelope; the receiver validates the schema before using it. */
    struct InventoryMessage {
        static constexpr std::uint32_t kMaximumBytes = 512 * 1024;
        std::string body;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            std::uint32_t size = write ? static_cast<std::uint32_t>(body.size()) : 0;
            valid              = (!write || body.size() <= kMaximumBytes) && stream->Serialize(write, size) && size <= kMaximumBytes;
            // A length the packet does not carry is refused before anything
            // is allocated for it: a few bytes must not cost the reader 512 KiB.
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

    /** Server to owner: the inventory, whole or as the rows that changed (see inventory_sync.h). */
    struct InventorySync: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventorySync");
    };

    struct CustomItemDefinitions: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CustomItemDefinitions");
    };

    struct CustomItemUse: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CustomItemUse");
    };

    /** Owner to server: a revision was projected, or a full sync is needed. */
    struct InventoryApplied: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryApplied");
    };

    /** Owner to server: rows the game wore down in use. */
    struct InventoryWear: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryWear");
    };
} // namespace KCDC::Shared::RPC
