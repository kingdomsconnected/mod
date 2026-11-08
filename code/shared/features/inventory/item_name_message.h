#pragma once

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Shared::RPC {
    /**
     * Server to client: what the game's item name keys say on this server.
     *
     * A key is an item class's `UIName`, without the '@', and the client
     * answers it wherever the game localizes it. An empty name gives the key
     * back to the game. `replace` sends the whole table, on connect, and
     * forgets every key it does not list; otherwise the entries change only
     * themselves.
     */
    struct ItemNames {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ItemNames");

        // The shipped tables hold 2196 distinct keys.
        static constexpr std::uint32_t kMaximumEntries = 4096;
        static constexpr std::size_t kMaximumKeyBytes  = 128;
        static constexpr std::size_t kMaximumNameBytes = 128;

        struct Entry {
            std::string key;
            std::string name;
        };

        bool replace = false;
        std::vector<Entry> entries;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(replace);
            std::uint32_t count = static_cast<std::uint32_t>(entries.size());
            fields.Field(count);
            // Refused before anything is allocated for it.
            if (!fields.Good() || count > kMaximumEntries) {
                _complete = false;
                return;
            }
            if (!write) {
                entries.resize(count);
            }
            for (auto &entry : entries) {
                fields.Field(entry.key);
                fields.Field(entry.name);
            }
            _complete = fields.Good();
        }

        bool IsSane() const {
            if (!_complete || entries.size() > kMaximumEntries) {
                return false;
            }
            for (const auto &entry : entries) {
                if (entry.key.empty() || entry.key.size() > kMaximumKeyBytes || entry.name.size() > kMaximumNameBytes) {
                    return false;
                }
            }
            return true;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
