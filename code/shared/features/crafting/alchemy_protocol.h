#pragma once

#include "alchemy_message.h"
#include "alchemy_native_ids.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace KCDC::Shared::Crafting {
    /** Exact wire identities are decimal strings, including on JavaScript. */
    inline bool ReadIdentity(const nlohmann::json &object, const char *key, std::uint64_t &value) {
        const auto found = object.find(key);
        if (found == object.end() || !found->is_string()) {
            return false;
        }
        const auto &encoded = found->get_ref<const std::string &>();
        if (encoded.empty() || encoded.size() > 20 || (encoded.size() > 1 && encoded.front() == '0')) {
            return false;
        }
        const auto result = std::from_chars(encoded.data(), encoded.data() + encoded.size(), value);
        return result.ec == std::errc() && result.ptr == encoded.data() + encoded.size();
    }

    inline bool IsToken(const nlohmann::json &object, const char *key, std::size_t maximum = 64) {
        const auto found = object.find(key);
        if (found == object.end() || !found->is_string()) {
            return false;
        }
        const auto &text = found->get_ref<const std::string &>();
        return !text.empty() && text.size() <= maximum && std::all_of(text.begin(), text.end(), [](unsigned char value) {
            return value >= 33 && value <= 126;
        });
    }

    inline bool HasOnly(const nlohmann::json &object, std::initializer_list<std::string_view> fields) {
        if (!object.is_object()) {
            return false;
        }
        for (const auto &entry : object.items()) {
            if (std::find(fields.begin(), fields.end(), entry.key()) == fields.end()) {
                return false;
            }
        }
        return true;
    }

    /** A per-batch counter, carried as a decimal string. */
    inline bool ReadSequence(const nlohmann::json &object, std::uint64_t &value) {
        return ReadIdentity(object, "sequence", value) && value != 0;
    }

    /**
     * Admission shape only; the server owns every rule. A request names one
     * native intent of the connection that sent it: no identity, inventory
     * revision, item count or result travels upstream.
     */
    inline bool ReadRequest(const std::string &body, nlohmann::json &request, std::string &error) {
        error = "invalidAlchemyRequest";
        if (body.empty() || body.size() > KCDC::Shared::RPC::AlchemyRequest::kMaximumBytes) {
            return false;
        }
        bool duplicateKey = false;
        std::vector<std::set<std::string>> objectKeys;
        auto parsed = nlohmann::json::parse(
            body,
            [&](int, nlohmann::json::parse_event_t event, nlohmann::json &value) {
                if (event == nlohmann::json::parse_event_t::object_start) {
                    objectKeys.emplace_back();
                }
                else if (event == nlohmann::json::parse_event_t::key) {
                    duplicateKey |= !objectKeys.back().insert(value.get<std::string>()).second;
                }
                else if (event == nlohmann::json::parse_event_t::object_end) {
                    objectKeys.pop_back();
                }
                return true;
            },
            false);
        if (duplicateKey || !parsed.is_object() || !IsToken(parsed, "request") || !IsToken(parsed, "op")) {
            return false;
        }
        const auto &operation = parsed.at("op").get_ref<const std::string &>();
        std::uint64_t value   = 0;
        if (operation == "start") {
            if (!HasOnly(parsed, {"request", "op", "station", "ticket", "callbackGeneration", "continuationOf"}) || !ReadIdentity(parsed, "station", value) || value == 0 || !ReadIdentity(parsed, "callbackGeneration", value) || value == 0 || !parsed.contains("ticket")
                || !parsed.at("ticket").is_number_unsigned() || parsed.at("ticket").get<std::uint64_t>() == 0 || parsed.at("ticket").get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            if (parsed.contains("continuationOf") && !IsToken(parsed, "continuationOf", 160)) {
                return false;
            }
        }
        else if (operation == "action") {
            if (!HasOnly(parsed, {"request", "op", "session", "sequence", "target", "itemId"}) || !IsToken(parsed, "session", 160) || !ReadSequence(parsed, value) || !parsed.contains("target") || !parsed.at("target").is_number_unsigned()
                || parsed.at("target").get<std::uint64_t>() >= static_cast<std::uint32_t>(AlchemyPosition::None)) {
                return false;
            }
            if (parsed.contains("itemId") && !IsToken(parsed, "itemId", 64)) {
                return false;
            }
        }
        else if (operation == "complete" || operation == "return" || operation == "entryready" || operation == "pulse") {
            if (!HasOnly(parsed, {"request", "op", "session", "sequence"}) || !IsToken(parsed, "session", 160) || !ReadSequence(parsed, value)) {
                return false;
            }
        }
        else if (operation == "cancel") {
            if (!HasOnly(parsed, {"request", "op", "session", "startRequest", "continuationOf", "reason"}) || (!IsToken(parsed, "session", 160) && !IsToken(parsed, "startRequest"))) {
                return false;
            }
            if (parsed.contains("reason") && parsed.at("reason") != "cancelled" && parsed.at("reason") != "interrupted" && parsed.at("reason") != "clientError") {
                return false;
            }
            if ((parsed.contains("startRequest") && !IsToken(parsed, "startRequest")) || (parsed.contains("session") && !IsToken(parsed, "session", 160)) || (parsed.contains("continuationOf") && !IsToken(parsed, "continuationOf", 160))) {
                return false;
            }
        }
        else if (operation == "inspect") {
            if (!HasOnly(parsed, {"request", "op"})) {
                return false;
            }
        }
        else {
            return false;
        }
        request = std::move(parsed);
        error.clear();
        return true;
    }
} // namespace KCDC::Shared::Crafting
