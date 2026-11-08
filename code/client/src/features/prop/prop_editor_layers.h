#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KCDC::Features::Prop {
    struct EditorLayer {
        std::string id;
        std::string name;
        bool visible = true;
        bool locked  = false;
    };

    /** Project layer definitions and session object membership. Unassigned
     * objects belong to Default; a custom layer is independent of Default.
     * Object existence and edit permissions belong to the command dispatcher.
     */
    class EditorLayers final {
      public:
        static constexpr std::size_t kMaxLayers    = 256;
        static constexpr std::size_t kMaxMembers   = 100000;
        static constexpr std::size_t kMaxNameBytes = 128;

        const std::vector<EditorLayer> &Layers() const {
            return _layers;
        }
        const std::unordered_map<std::string, std::string> &Members() const {
            return *_members;
        }
        const std::string &Active() const {
            return _active;
        }
        std::uint64_t Revision() const {
            return _revision;
        }

        /** Undo restores content without recycling identifiers allocated later. */
        void Restore(const EditorLayers &state) {
            const auto nextId   = std::max(_nextId, state._nextId);
            const auto revision = _revision + 1;
            *this               = state;
            _nextId             = nextId;
            _revision           = revision;
        }

        const EditorLayer *Find(const std::string &id) const {
            const auto found = std::find_if(_layers.begin(), _layers.end(), [&](const EditorLayer &layer) {
                return layer.id == id;
            });
            return found == _layers.end() ? nullptr : &*found;
        }

        std::string LayerOf(const std::string &key) const {
            const auto found = _members->find(key);
            return found == _members->end() ? "default" : found->second;
        }

        bool Visible(const std::string &key) const {
            return Find(LayerOf(key))->visible;
        }
        bool Editable(const std::string &key) const {
            const auto *layer = Find(LayerOf(key));
            return layer->visible && !layer->locked;
        }

        std::string Create(const std::string &name, std::string &error) {
            error.clear();
            if (!ValidateName(name, "", error)) {
                return {};
            }
            if (_layers.size() >= kMaxLayers) {
                error = "Too many layers.";
                return {};
            }
            if (_nextId == std::numeric_limits<std::uint64_t>::max()) {
                error = "Layer identifier limit reached.";
                return {};
            }
            const std::string id = "layer-" + std::to_string(_nextId++);
            _layers.push_back({id, name});
            ++_revision;
            return id;
        }

        bool Rename(const std::string &id, const std::string &name, std::string &error) {
            error.clear();
            auto *layer = MutableFind(id, error);
            if (!layer) {
                return false;
            }
            if (id == "default") {
                error = "Default cannot be renamed.";
                return false;
            }
            if (!ValidateName(name, id, error)) {
                return false;
            }
            if (layer->name != name) {
                layer->name = name;
                ++_revision;
            }
            return true;
        }

        /** Deleting a layer keeps its objects and returns them to Default. */
        bool Delete(const std::string &id, bool confirmed, std::string &error) {
            error.clear();
            if (!Find(id)) {
                error = "Unknown layer.";
                return false;
            }
            if (id == "default") {
                error = "Default cannot be deleted.";
                return false;
            }
            if (!confirmed) {
                error = "Deleting a layer requires confirmation.";
                return false;
            }
            if (std::any_of(_members->begin(), _members->end(), [&](const auto &member) {
                    return member.second == id;
                })) {
                auto &members = MutableMembers();
                for (auto member = members.begin(); member != members.end();) {
                    if (member->second == id) {
                        member = members.erase(member);
                    }
                    else {
                        ++member;
                    }
                }
            }
            _layers.erase(std::remove_if(_layers.begin(), _layers.end(),
                              [&](const EditorLayer &layer) {
                                  return layer.id == id;
                              }),
                _layers.end());
            if (_active == id) {
                _active = "default";
            }
            ++_revision;
            return true;
        }

        bool SetVisible(const std::string &id, bool visible, std::string &error) {
            error.clear();
            auto *layer = MutableFind(id, error);
            if (!layer) {
                return false;
            }
            if (layer->visible != visible) {
                layer->visible = visible;
                ++_revision;
            }
            return true;
        }

        bool SetLocked(const std::string &id, bool locked, std::string &error) {
            error.clear();
            auto *layer = MutableFind(id, error);
            if (!layer) {
                return false;
            }
            if (layer->locked != locked) {
                layer->locked = locked;
                ++_revision;
            }
            return true;
        }

        bool SetActive(const std::string &id, std::string &error) {
            error.clear();
            if (!Find(id)) {
                error = "Unknown layer.";
                return false;
            }
            if (_active != id) {
                _active = id;
                ++_revision;
            }
            return true;
        }

        /** Atomic membership update. The caller validates object existence and
         * edit permissions before invoking this or restoring a snapshot.
         */
        bool Assign(const std::vector<std::string> &keys, const std::string &target, std::string &error) {
            error.clear();
            if (!Find(target)) {
                error = "Unknown layer.";
                return false;
            }
            if (keys.size() > kMaxMembers) {
                error = "Too many layer members.";
                return false;
            }
            std::unordered_set<std::string> seen;
            std::size_t additions = 0;
            bool changed          = false;
            for (const auto &key : keys) {
                if (!ValidObjectKey(key)) {
                    error = "Invalid layer object key.";
                    return false;
                }
                if (!seen.insert(key).second) {
                    error = "Duplicate layer object key.";
                    return false;
                }
                if (_members->find(key) == _members->end()) {
                    ++additions;
                }
                changed = changed || LayerOf(key) != target;
            }
            if (target != "default" && _members->size() + additions > kMaxMembers) {
                error = "Too many layer members.";
                return false;
            }
            if (!changed) {
                return true;
            }
            auto &members = MutableMembers();
            for (const auto &key : keys) {
                if (LayerOf(key) == target) {
                    continue;
                }
                if (target == "default") {
                    members.erase(key);
                }
                else {
                    members[key] = target;
                }
            }
            ++_revision;
            return true;
        }

        /** Removes a deleted object's session membership; history owns its former layer. */
        void Forget(const std::string &key) {
            if (!_members->contains(key)) {
                return;
            }
            MutableMembers().erase(key);
            ++_revision;
        }

        nlohmann::json ToJson(bool includeMembers = true) const {
            nlohmann::json result = {{"layers", nlohmann::json::array()}, {"active", _active}, {"nextId", _nextId}};
            for (const auto &layer : _layers) {
                result["layers"].push_back({{"id", layer.id}, {"name", layer.name}, {"visible", layer.visible}, {"locked", layer.locked}});
            }
            if (includeMembers) {
                result["members"] = nlohmann::json::array();
                std::vector<std::string> keys;
                keys.reserve(_members->size());
                for (const auto &member : *_members) {
                    keys.push_back(member.first);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto &key : keys) {
                    result["members"].push_back({{"key", key}, {"layer", _members->at(key)}});
                }
            }
            return result;
        }

        /** Parses into a temporary so malformed files cannot partially apply. */
        static bool FromJson(const nlohmann::json &json, EditorLayers &out, std::string &error) {
            error.clear();
            try {
                if (!json.is_object() || !json.contains("layers") || !json.at("layers").is_array()) {
                    throw std::invalid_argument("Layer definitions must be an array.");
                }
                const auto &definitions = json.at("layers");
                if (definitions.empty() || definitions.size() > kMaxLayers) {
                    throw std::invalid_argument("Invalid layer count.");
                }
                EditorLayers next;
                next._layers.clear();
                std::uint64_t largestId = 0;
                bool hasDefault         = false;
                for (const auto &entry : definitions) {
                    if (!entry.is_object()) {
                        throw std::invalid_argument("Layer definition must be an object.");
                    }
                    EditorLayer layer {entry.at("id").get<std::string>(), entry.at("name").get<std::string>(), entry.value("visible", true), entry.value("locked", false)};
                    if (next.Find(layer.id)) {
                        throw std::invalid_argument("Duplicate layer identifier.");
                    }
                    if (!next.ValidateName(layer.name, "", error)) {
                        throw std::invalid_argument(error);
                    }
                    if (layer.id == "default") {
                        if (layer.name != "Default") {
                            throw std::invalid_argument("Default cannot be renamed.");
                        }
                        hasDefault = true;
                    }
                    else {
                        std::uint64_t number = 0;
                        if (layer.id.compare(0, 6, "layer-") != 0 || !PositiveInteger(layer.id.substr(6), number)) {
                            throw std::invalid_argument("Invalid layer identifier.");
                        }
                        largestId = std::max(largestId, number);
                    }
                    next._layers.push_back(std::move(layer));
                }
                if (!hasDefault) {
                    throw std::invalid_argument("Default layer is required.");
                }
                if (!json.contains("nextId") || !json.at("nextId").is_number_integer()) {
                    throw std::invalid_argument("Invalid next layer identifier.");
                }
                if (!json.at("nextId").is_number_unsigned() && json.at("nextId").get<std::int64_t>() < 1) {
                    throw std::invalid_argument("Invalid next layer identifier.");
                }
                next._nextId = json.at("nextId").get<std::uint64_t>();
                if (next._nextId == 0 || next._nextId <= largestId) {
                    throw std::invalid_argument("Next layer identifier must exceed existing identifiers.");
                }
                next._active = json.value("active", std::string("default"));
                if (!next.Find(next._active)) {
                    throw std::invalid_argument("Active layer does not exist.");
                }
                if (json.contains("members")) {
                    const auto &members = json.at("members");
                    if (!members.is_array() || members.size() > kMaxMembers) {
                        throw std::invalid_argument("Invalid layer members.");
                    }
                    std::unordered_set<std::string> seen;
                    for (const auto &entry : members) {
                        const auto key   = entry.at("key").get<std::string>();
                        const auto layer = entry.at("layer").get<std::string>();
                        if (!ValidObjectKey(key)) {
                            throw std::invalid_argument("Invalid layer object key.");
                        }
                        if (!seen.insert(key).second) {
                            throw std::invalid_argument("Duplicate layer object key.");
                        }
                        if (!next.Find(layer)) {
                            throw std::invalid_argument("Layer member references an unknown layer.");
                        }
                        if (layer != "default") {
                            next._members->emplace(key, layer);
                        }
                    }
                }
                next._revision = out._revision + 1;
                out            = std::move(next);
                error.clear();
                return true;
            }
            catch (const std::exception &exception) {
                error = exception.what();
                return false;
            }
        }

      private:
        std::vector<EditorLayer> _layers {{"default", "Default"}};
        // History and published snapshots share membership until an assignment
        // changes it. Visibility, locks, names and active-layer changes stay cheap.
        std::shared_ptr<std::unordered_map<std::string, std::string>> _members = std::make_shared<std::unordered_map<std::string, std::string>>();
        std::string _active                                                    = "default";
        std::uint64_t _nextId                                                  = 1;
        std::uint64_t _revision                                                = 0;

        std::unordered_map<std::string, std::string> &MutableMembers() {
            if (_members.use_count() != 1) {
                _members = std::make_shared<std::unordered_map<std::string, std::string>>(*_members);
            }
            return *_members;
        }

        EditorLayer *MutableFind(const std::string &id, std::string &error) {
            for (auto &layer : _layers) {
                if (layer.id == id) {
                    return &layer;
                }
            }
            error = "Unknown layer.";
            return nullptr;
        }

        bool ValidateName(const std::string &name, const std::string &except, std::string &error) const {
            if (name.empty() || name.size() > kMaxNameBytes || name.front() == ' ' || name.back() == ' ' || std::any_of(name.begin(), name.end(), [](unsigned char c) {
                    return c < 0x20 || c == 0x7F;
                })) {
                error = "Layer names must contain 1 to 128 bytes, without control characters or leading or trailing spaces.";
                return false;
            }
            for (const auto &layer : _layers) {
                if (layer.id != except && layer.name == name) {
                    error = "A layer with this name already exists.";
                    return false;
                }
            }
            return true;
        }

        static bool PositiveInteger(const std::string &text, std::uint64_t &number) {
            if (text.empty() || text.front() == '0') {
                return false;
            }
            number = 0;
            for (const unsigned char character : text) {
                if (character < '0' || character > '9') {
                    return false;
                }
                const auto digit = static_cast<std::uint64_t>(character - '0');
                if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                    return false;
                }
                number = number * 10 + digit;
            }
            return true;
        }

        static bool ValidObjectKey(const std::string &key) {
            if (key.size() > 512) {
                return false;
            }
            const auto separator = key.find(':');
            if (separator == std::string::npos || separator + 1 == key.size()) {
                return false;
            }
            const auto kind = key.substr(0, separator);
            const auto id   = key.substr(separator + 1);
            if (kind == "prop" || kind == "effect" || kind == "light" || kind == "world") {
                std::uint64_t number = 0;
                return PositiveInteger(id, number);
            }
            if (kind != "area" && kind != "patrol") {
                return false;
            }
            return std::none_of(id.begin(), id.end(), [](unsigned char c) {
                return c < 0x20 || c == 0x7F;
            });
        }
    };
} // namespace KCDC::Features::Prop
