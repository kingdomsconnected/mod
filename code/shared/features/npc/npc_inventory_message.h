#pragma once

#include "shared/features/inventory/inventory_message.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <set>
#include <string>

namespace KCDC::Shared::RPC {
    struct NpcInventoryRequest: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcInventoryRequest");
    };
    struct NpcInventoryReply: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcInventoryReply");
    };
} // namespace KCDC::Shared::RPC

namespace KCDC::Shared::NpcInventory {
    inline constexpr std::size_t kMaximumRows         = 4096;
    inline constexpr std::uint32_t kMaximumAmount     = 2147483647U;
    inline constexpr std::uint64_t kLeaseMilliseconds = 120000;
    inline constexpr float kUseDistance               = 4.0F;

    enum class Action : std::uint8_t {
        Seed,
        Open,
        Harvest,
        Complete,
        Cancel,
        Close,
        Renew,
        Transfer
    };

    inline bool Unsigned(const nlohmann::json &value, const char *key, std::uint64_t &out, std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max()) {
        const auto found = value.find(key);
        if (found == value.end() || !found->is_number_unsigned()) {
            return false;
        }
        out = found->get<std::uint64_t>();
        return out <= maximum;
    }

    /** The grant and butcher phase travel with the stock they describe. */
    struct InteractionState {
        std::uint64_t holder = 0;
        std::uint64_t token  = 0;
        bool harvesting      = false;
        bool harvested       = false;

        bool operator==(const InteractionState &) const = default;

        bool BusyFor(std::uint64_t ownToken) const {
            return holder != 0 && (ownToken == 0 || ownToken != token);
        }

        bool Read(const nlohmann::json &snapshot) {
            InteractionState next;
            if (!snapshot.is_object() || !Unsigned(snapshot, "holder", next.holder) || !Unsigned(snapshot, "token", next.token) || !snapshot.contains("harvesting") || !snapshot.at("harvesting").is_boolean() || !snapshot.contains("harvested")
                || !snapshot.at("harvested").is_boolean()) {
                return false;
            }
            next.harvesting = snapshot.at("harvesting").get<bool>();
            next.harvested  = snapshot.at("harvested").get<bool>();
            if ((next.holder == 0) != (next.token == 0) || (next.harvesting && (next.holder == 0 || next.harvested))) {
                return false;
            }
            *this = next;
            return true;
        }
    };

    /** A request never names a player: the connection supplies its identity. */
    inline bool ReadRequest(const nlohmann::json &body, std::uint64_t &npc, std::uint32_t &epoch, std::uint32_t &request, Action &action, std::uint64_t &token) {
        std::uint64_t wireEpoch = 0, wireRequest = 0, wireAction = 0;
        if (!body.is_object() || !Unsigned(body, "npc", npc) || npc == 0 || !Unsigned(body, "epoch", wireEpoch, UINT32_MAX) || wireEpoch == 0 || !Unsigned(body, "request", wireRequest, UINT32_MAX) || wireRequest == 0
            || !Unsigned(body, "action", wireAction, static_cast<std::uint8_t>(Action::Transfer)) || !Unsigned(body, "token", token)) {
            return false;
        }
        epoch   = static_cast<std::uint32_t>(wireEpoch);
        request = static_cast<std::uint32_t>(wireRequest);
        action  = static_cast<Action>(wireAction);
        return true;
    }

    inline bool ValidUnits(const nlohmann::json &units) {
        if (!units.is_array() || units.empty() || units.size() > kMaximumRows) {
            return false;
        }
        std::set<std::string> named;
        for (const auto &unit : units) {
            std::uint64_t amount = 0;
            if (!unit.is_object() || !unit.contains("id") || !unit.at("id").is_string() || !Unsigned(unit, "amount", amount, kMaximumAmount) || amount == 0) {
                return false;
            }
            const std::string id = unit.at("id").get<std::string>();
            if (id.empty() || id.size() > 64 || !named.insert(id).second) {
                return false;
            }
        }
        return true;
    }
} // namespace KCDC::Shared::NpcInventory
