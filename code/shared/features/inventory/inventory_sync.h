#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace KCDC::Shared::Inventory {
    /** Bounds a client holds the server to; they match the server's own inventory limits. */
    inline constexpr std::size_t kMaximumSyncRows     = 4096;
    inline constexpr std::size_t kMaximumSyncIdLength = 64;

    /**
     * What goes to the owner when their inventory changes.
     *
     * Rows are keyed by identity and carry `item`, `amount` and `metadata`.
     * With no `base` it is the whole inventory; otherwise it holds only the
     * rows that changed since `base`, which the client must be at. Messages
     * travel reliable and ordered, so a delta always meets its base unless
     * the client started over, and then it asks for a full one.
     */
    inline nlohmann::json EncodeSync(std::optional<std::uint64_t> base, std::uint64_t revision, const std::map<std::string, nlohmann::json> &before, const std::map<std::string, nlohmann::json> &after) {
        if (!base) {
            nlohmann::json rows = nlohmann::json::object();
            for (const auto &[id, row] : after) {
                rows[id] = row;
            }
            return {{"full", true}, {"revision", revision}, {"rows", std::move(rows)}};
        }
        nlohmann::json upsert = nlohmann::json::object();
        nlohmann::json remove = nlohmann::json::array();
        for (const auto &[id, row] : after) {
            const auto previous = before.find(id);
            if (previous == before.end() || previous->second != row) {
                upsert[id] = row;
            }
        }
        for (const auto &[id, row] : before) {
            if (!after.contains(id)) {
                remove.push_back(id);
            }
        }
        return {{"full", false}, {"base", *base}, {"revision", revision}, {"upsert", std::move(upsert)}, {"remove", std::move(remove)}};
    }

    /** A row the client can project: a class GUID, a positive amount and a metadata object. */
    inline bool IsSyncRow(const std::string &id, const nlohmann::json &row) {
        if (id.empty() || id.size() > kMaximumSyncIdLength || !row.is_object() || row.size() != 3) {
            return false;
        }
        const auto item     = row.find("item");
        const auto amount   = row.find("amount");
        const auto metadata = row.find("metadata");
        return item != row.end() && item->is_string() && amount != row.end() && amount->is_number_unsigned() && amount->get<std::uint64_t>() != 0 && amount->get<std::uint64_t>() <= 2147483647ULL && metadata != row.end() && metadata->is_object();
    }

    /**
     * Applies a sync message to the client's copy. Refuses a malformed
     * message or a delta on another base, and then changes nothing.
     */
    inline bool ApplySync(const nlohmann::json &message, std::uint64_t &revision, std::map<std::string, nlohmann::json> &rows) {
        if (!message.is_object() || !message.contains("full") || !message.at("full").is_boolean() || !message.contains("revision") || !message.at("revision").is_number_unsigned()) {
            return false;
        }
        std::map<std::string, nlohmann::json> next;
        if (message.at("full").get<bool>()) {
            const auto found = message.find("rows");
            if (found == message.end() || !found->is_object()) {
                return false;
            }
            for (const auto &[id, row] : found->items()) {
                if (!IsSyncRow(id, row)) {
                    return false;
                }
                next.emplace(id, row);
            }
        }
        else {
            const auto base   = message.find("base");
            const auto upsert = message.find("upsert");
            const auto remove = message.find("remove");
            if (base == message.end() || !base->is_number_unsigned() || base->get<std::uint64_t>() != revision || upsert == message.end() || !upsert->is_object() || remove == message.end() || !remove->is_array()) {
                return false;
            }
            next = rows;
            for (const auto &id : *remove) {
                if (!id.is_string() || next.erase(id.get<std::string>()) == 0) {
                    return false;
                }
            }
            for (const auto &[id, row] : upsert->items()) {
                if (!IsSyncRow(id, row)) {
                    return false;
                }
                next.insert_or_assign(id, row);
            }
        }
        if (next.size() > kMaximumSyncRows) {
            return false;
        }
        rows     = std::move(next);
        revision = message.at("revision").get<std::uint64_t>();
        return true;
    }

    /** The most units of one class a sync can announce: every row full. */
    inline constexpr std::int64_t kMaximumSyncNotice = static_cast<std::int64_t>(kMaximumSyncRows) * 2147483647LL;

    /**
     * What the owner's game announces with a sync, as its own item-received
     * and item-removed toasts: units per class, gained positive and lost
     * negative. Only changes their cause asked to be announced are in it.
     */
    inline nlohmann::json EncodeNotices(const std::map<std::string, std::int64_t> &notices) {
        nlohmann::json encoded = nlohmann::json::array();
        for (const auto &[itemClass, amount] : notices) {
            encoded.push_back({{"item", itemClass}, {"amount", amount}});
        }
        return encoded;
    }

    /** A sync's notices, summed per class; a sync without any has none. False on a malformed list, which then announces nothing. */
    inline bool ReadNotices(const nlohmann::json &message, std::map<std::string, std::int64_t> &notices) {
        notices.clear();
        const auto found = message.find("notices");
        if (found == message.end()) {
            return true;
        }
        if (!found->is_array() || found->size() > kMaximumSyncRows) {
            return false;
        }
        std::map<std::string, std::int64_t> read;
        for (const auto &notice : *found) {
            const auto item   = notice.is_object() ? notice.find("item") : notice.end();
            const auto amount = notice.is_object() ? notice.find("amount") : notice.end();
            if (item == notice.end() || !item->is_string() || amount == notice.end() || !amount->is_number_integer()) {
                return false;
            }
            if (amount->is_number_unsigned() && amount->get<std::uint64_t>() > static_cast<std::uint64_t>(kMaximumSyncNotice)) {
                return false;
            }
            const auto count = amount->get<std::int64_t>();
            if (count == 0 || count < -kMaximumSyncNotice || count > kMaximumSyncNotice || !read.emplace(item->get<std::string>(), count).second) {
                return false;
            }
        }
        notices = std::move(read);
        return true;
    }
} // namespace KCDC::Shared::Inventory
