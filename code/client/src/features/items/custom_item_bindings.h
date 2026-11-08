#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace KCDC::Features::Items {
    /** Instance text shares the binding's lifetime; no separate label cache. */
    class CustomItemBindings final {
      public:
        struct Binding {
            std::string nativeGuid;
            nlohmann::json custom;
            std::string stackKey;
            std::string name;
            std::string description;
            bool stackable;
            std::string listGroupKey;
            std::uint32_t listClassId;
        };

        bool Bind(std::uint64_t id, const std::string &nativeGuid, const nlohmann::json &custom, const std::string &stackKey, const std::string &name, const std::string &description, bool stackable = false) {
            const auto *old = Find(id, nativeGuid);
            if (old != nullptr && old->custom == custom && old->stackKey == stackKey && old->name == name && old->description == description && old->stackable == stackable) {
                return false;
            }
            // Flash compares ClassId before collecting native WUIDs into a row.
            // Include all public properties and the opaque private equality key.
            const auto key = stackable ? nlohmann::json::array({nativeGuid, custom, stackKey}).dump() : nlohmann::json::array({nativeGuid, id}).dump();
            auto group     = _listGroups.find(key);
            if (group == _listGroups.end()) {
                // Native class indices start at zero; reserve the upper half
                // for custom groups, excluding the native invalid index (-1).
                if (_nextListClassId <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
                    throw std::overflow_error("Custom item list identity exhausted");
                }
                group = _listGroups.emplace(key, ListGroup {_nextListClassId--, 0}).first;
            }
            ++group->second.references;
            if (const auto previous = _bindings.find(id); previous != _bindings.end()) {
                ReleaseListGroup(previous->second.listGroupKey);
            }
            _bindings.insert_or_assign(id, Binding {nativeGuid, custom, stackKey, name, description, stackable, key, group->second.id});
            return true;
        }

        const Binding *Find(std::uint64_t id, std::string_view nativeGuid) const {
            const auto found = _bindings.find(id);
            return found != _bindings.end() && found->second.nativeGuid == nativeGuid ? &found->second : nullptr;
        }

        void Copy(std::uint64_t source, std::uint64_t destination, std::string_view nativeGuid) {
            if (const auto *binding = Find(source, nativeGuid)) {
                Bind(destination, binding->nativeGuid, binding->custom, binding->stackKey, binding->name, binding->description, binding->stackable);
            }
        }

        std::optional<std::uint32_t> ListClassId(std::uint64_t id, std::string_view nativeGuid) const {
            const auto *binding = Find(id, nativeGuid);
            return binding != nullptr ? std::optional<std::uint32_t>(binding->listClassId) : std::nullopt;
        }

        bool ResolveLabel(std::string_view label, std::string &out) const {
            if (label.starts_with('@')) {
                label.remove_prefix(1);
            }
            if (!label.starts_with("kcdc_ii_") || label.size() < 11 || (label.substr(label.size() - 2) != "_n" && label.substr(label.size() - 2) != "_d")) {
                return false;
            }
            const auto digits = label.substr(8, label.size() - 10);
            std::uint64_t id  = 0;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), id);
            if (parsed.ec != std::errc {} || parsed.ptr != digits.data() + digits.size() || (digits.size() > 1 && digits.front() == '0')) {
                return false;
            }
            const auto found = _bindings.find(id);
            if (found == _bindings.end()) {
                return false;
            }
            out = label.back() == 'd' ? found->second.description : found->second.name;
            return true;
        }

        /** Checks at most budget live WUIDs, resuming fairly across ticks. */
        template <typename TLive>
        std::size_t Prune(TLive &&live, std::size_t budget = 64) {
            const auto count    = std::min(budget, _bindings.size());
            std::size_t removed = 0;
            for (std::size_t checked = 0; checked < count; ++checked) {
                auto found = _lastSweptId ? _bindings.upper_bound(*_lastSweptId) : _bindings.begin();
                if (found == _bindings.end()) {
                    found = _bindings.begin();
                }
                _lastSweptId = found->first;
                if (!live(found->first, found->second.nativeGuid)) {
                    ReleaseListGroup(found->second.listGroupKey);
                    _bindings.erase(found);
                    ++removed;
                }
            }
            return removed;
        }

        void Reset() {
            _bindings.clear();
            _listGroups.clear();
            _nextListClassId = std::numeric_limits<std::uint32_t>::max() - 1;
            _lastSweptId.reset();
        }

        std::size_t Size() const {
            return _bindings.size();
        }

      private:
        struct ListGroup {
            std::uint32_t id;
            std::size_t references;
        };

        void ReleaseListGroup(const std::string &key) {
            const auto group = _listGroups.find(key);
            if (--group->second.references == 0) {
                _listGroups.erase(group);
            }
        }

        std::map<std::uint64_t, Binding> _bindings;
        std::map<std::string, ListGroup> _listGroups;
        std::uint32_t _nextListClassId = std::numeric_limits<std::uint32_t>::max() - 1;
        std::optional<std::uint64_t> _lastSweptId;
    };
} // namespace KCDC::Features::Items
