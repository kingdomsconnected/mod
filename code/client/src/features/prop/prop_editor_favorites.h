#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Prop {
    struct FavoriteFolder {
        std::uint32_t id = 1;
        std::string name = "Favorites";
        std::set<std::string> paths;

        bool operator==(const FavoriteFolder &) const = default;
    };

    /** Project-owned favorites. Catalog changes never discard unknown asset paths. */
    class FavoriteFolders final {
      public:
        static constexpr std::uint32_t kRoot     = 1;
        static constexpr std::size_t kMaxFolders = 256;
        static constexpr std::size_t kMaxPaths   = 4096;

        const std::vector<FavoriteFolder> &Folders() const {
            return _folders;
        }
        std::uint32_t ActiveId() const {
            return _active;
        }
        /** Undo restores content without reusing identifiers issued later in this project. */
        bool Restore(const FavoriteFolders &state) {
            const auto nextId = std::max(_nextId, state._nextId);
            if (_folders == state._folders && _active == state._active && _nextId == nextId) {
                return false;
            }
            *this   = state;
            _nextId = nextId;
            return true;
        }
        const FavoriteFolder *Find(std::uint32_t id) const {
            const auto found = std::find_if(_folders.begin(), _folders.end(), [id](const auto &folder) {
                return folder.id == id;
            });
            return found == _folders.end() ? nullptr : &*found;
        }
        bool Contains(const std::string &path, std::uint32_t folderId) const {
            const auto *folder = Find(folderId);
            return folder && folder->paths.contains(path);
        }
        bool SetActive(std::uint32_t id, std::string &error) {
            error.clear();
            if (!Find(id)) {
                error = "Favorite folder does not exist.";
                return false;
            }
            _active = id;
            return true;
        }
        std::uint32_t Add(const std::string &name, std::string &error) {
            error.clear();
            if (!ValidName(name) || NameExists(name)) {
                error = "Choose a unique folder name (1-80 characters, without control characters).";
                return 0;
            }
            if (_folders.size() >= kMaxFolders || _nextId == std::numeric_limits<std::uint32_t>::max()) {
                error = "Favorite folder limit reached.";
                return 0;
            }
            const auto id = _nextId++;
            _folders.push_back({id, name, {}});
            _active = id;
            return id;
        }
        bool Rename(std::uint32_t id, const std::string &name, std::string &error) {
            error.clear();
            auto *folder = MutableFind(id);
            if (!folder || id == kRoot) {
                error = "Choose a custom favorite folder.";
                return false;
            }
            if (!ValidName(name) || (name != folder->name && NameExists(name))) {
                error = "Choose a unique folder name (1-80 characters, without control characters).";
                return false;
            }
            folder->name = name;
            return true;
        }
        bool Delete(std::uint32_t id, std::string &error) {
            error.clear();
            if (id == kRoot || !Find(id)) {
                error = "Choose a custom favorite folder.";
                return false;
            }
            std::erase_if(_folders, [id](const auto &folder) {
                return folder.id == id;
            });
            if (_active == id) {
                _active = kRoot;
            }
            return true;
        }
        bool Set(const std::string &path, bool favorite, std::uint32_t folderId, std::string &error) {
            error.clear();
            auto *folder = MutableFind(folderId);
            if (!folder) {
                error = "Favorite folder does not exist.";
                return false;
            }
            if (!ValidPath(path)) {
                error = "Favorite asset path must contain 1-1024 characters without control characters.";
                return false;
            }
            if (favorite) {
                if (!folder->paths.contains(path) && folder->paths.size() >= kMaxPaths) {
                    error = "Favorite folder is full.";
                    return false;
                }
                folder->paths.insert(path);
            }
            else {
                folder->paths.erase(path);
            }
            return true;
        }
        bool Move(const std::string &path, std::uint32_t sourceId, std::uint32_t targetId, std::string &error) {
            error.clear();
            auto *source = MutableFind(sourceId);
            if (!source || !source->paths.contains(path)) {
                error = "Asset is not in the source favorite folder.";
                return false;
            }
            if (!Set(path, true, targetId, error)) {
                return false;
            }
            if (sourceId != targetId) {
                source->paths.erase(path);
            }
            return true;
        }

        nlohmann::json ToJson() const {
            auto folders = nlohmann::json::array();
            for (const auto &folder : _folders) {
                folders.push_back({{"id", folder.id}, {"name", folder.name}, {"paths", folder.paths}});
            }
            return {{"version", 1}, {"active", _active}, {"nextId", _nextId}, {"folders", std::move(folders)}};
        }
        /** Transactional strict parse; malformed input leaves the destination unchanged. */
        static bool FromJson(const nlohmann::json &root, FavoriteFolders &result, std::string &error) {
            error.clear();
            const auto fail = [&error]() {
                error = "Invalid project favorite folders.";
                return false;
            };
            if (!root.is_object() || !root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1 || !root.contains("active") || !root.contains("nextId") || !root.contains("folders")) {
                return fail();
            }
            FavoriteFolders parsed;
            if (!ReadId(root["active"], parsed._active) || !ReadId(root["nextId"], parsed._nextId)) {
                return fail();
            }
            const auto &folders = root["folders"];
            if (!folders.is_array() || folders.empty() || folders.size() > kMaxFolders) {
                return fail();
            }
            parsed._folders.clear();
            for (const auto &entry : folders) {
                if (!entry.is_object() || !entry.contains("id") || !entry.contains("name") || !entry["name"].is_string() || !entry.contains("paths") || !entry["paths"].is_array() || entry["paths"].size() > kMaxPaths) {
                    return fail();
                }
                FavoriteFolder folder;
                if (!ReadId(entry["id"], folder.id) || folder.id >= parsed._nextId || parsed.Find(folder.id)) {
                    return fail();
                }
                folder.name = entry["name"].get<std::string>();
                if (!ValidName(folder.name) || parsed.NameExists(folder.name) || (folder.id == kRoot && folder.name != "Favorites")) {
                    return fail();
                }
                for (const auto &path : entry["paths"]) {
                    if (!path.is_string()) {
                        return fail();
                    }
                    const auto value = path.get<std::string>();
                    if (!ValidPath(value) || !folder.paths.insert(value).second) {
                        return fail();
                    }
                }
                parsed._folders.push_back(std::move(folder));
            }
            if (!parsed.Find(kRoot) || !parsed.Find(parsed._active)) {
                return fail();
            }
            result = std::move(parsed);
            return true;
        }
        bool operator==(const FavoriteFolders &) const = default;

      private:
        FavoriteFolder *MutableFind(std::uint32_t id) {
            const auto found = std::find_if(_folders.begin(), _folders.end(), [id](const auto &folder) {
                return folder.id == id;
            });
            return found == _folders.end() ? nullptr : &*found;
        }
        bool NameExists(const std::string &name) const {
            return std::any_of(_folders.begin(), _folders.end(), [&name](const auto &folder) {
                return folder.name == name;
            });
        }
        static bool ValidText(const std::string &text, std::size_t limit) {
            return !text.empty() && text.size() <= limit && std::none_of(text.begin(), text.end(), [](unsigned char c) {
                return c < 32 || c == 127;
            });
        }
        static bool ValidName(const std::string &name) {
            return ValidText(name, 80) && name.front() != ' ' && name.back() != ' ';
        }
        static bool ValidPath(const std::string &path) {
            return ValidText(path, 1024);
        }
        static bool ReadId(const nlohmann::json &value, std::uint32_t &id) {
            if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() <= 0)) {
                return false;
            }
            const auto integer = value.get<std::uint64_t>();
            if (integer == 0 || integer > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            id = static_cast<std::uint32_t>(integer);
            return true;
        }
        std::vector<FavoriteFolder> _folders {FavoriteFolder {}};
        std::uint32_t _active = kRoot;
        std::uint32_t _nextId = kRoot + 1;
    };
} // namespace KCDC::Features::Prop
