#pragma once

#include "shared/game/item_stats.h"
#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace KCDC::Shared::RPC {
    /**
     * Server to every client: the combat numbers the server has rewritten on
     * item classes.
     *
     * A table the way `ItemNames` is one. A connection is handed the whole
     * table with `replace` set; a later change travels as the one class that
     * changed. An entry with no values gives the class its authored numbers
     * back. Every client applies it to the shared class row, so the hit
     * calculation, the inventory screen and the shops all read the new
     * numbers on their next look.
     */
    struct ItemStats {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ItemStats");

        // 1,500 or so equippable classes ship; a server rewriting every one is still far inside this.
        static constexpr std::uint32_t kMaximumEntries = 4096;

        struct Value {
            std::uint8_t stat = 0;
            float value       = 0.0F;
        };

        struct Entry {
            ItemClassId itemClass;
            std::vector<Value> values;
        };

        bool replace = false;
        std::vector<Entry> entries;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(replace);
            std::uint32_t count = static_cast<std::uint32_t>(entries.size());
            fields.Field(count);
            if (!fields.Good() || count > kMaximumEntries) {
                _complete = false;
                return;
            }
            if (!write) {
                entries.resize(count);
            }
            for (auto &entry : entries) {
                fields.Field(entry.itemClass.low);
                fields.Field(entry.itemClass.high);
                std::uint8_t valueCount = static_cast<std::uint8_t>(entry.values.size());
                fields.Field(valueCount);
                if (!fields.Good() || valueCount > Game::kItemStatCount) {
                    _complete = false;
                    return;
                }
                if (!write) {
                    entry.values.resize(valueCount);
                }
                for (auto &value : entry.values) {
                    fields.Field(value.stat);
                    fields.Field(value.value);
                }
            }
            _complete = fields.Good();
        }

        bool IsSane() const {
            if (!_complete || entries.size() > kMaximumEntries) {
                return false;
            }
            for (const auto &entry : entries) {
                if (!entry.itemClass.IsValid() || entry.values.size() > Game::kItemStatCount) {
                    return false;
                }
                for (const auto &value : entry.values) {
                    if (value.stat >= Game::kItemStatCount || !Game::ItemStatAccepts(static_cast<Game::ItemStat>(value.stat), value.value)) {
                        return false;
                    }
                }
            }
            return true;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
