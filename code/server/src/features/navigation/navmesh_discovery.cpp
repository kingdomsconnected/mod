#include "navmesh_discovery.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <system_error>
#include <utility>

namespace KCDC::Features::Navigation {
    namespace {
        void AddPak(std::vector<std::filesystem::path> &paks, const std::filesystem::path &path) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return;
            }
            const std::filesystem::path resolved = std::filesystem::canonical(path, error);
            if (!error && std::find(paks.begin(), paks.end(), resolved) == paks.end()) {
                paks.push_back(resolved);
            }
        }

        std::string Lowercase(const std::filesystem::path &part) {
            std::string text = part.string();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        /** How well a copy found below `files` fits the level: 0 under its name, 1 under no level's name, 2 the bare copy, or empty under another level's name. */
        std::optional<int> Rank(const std::filesystem::path &relative, const std::string &level) {
            // Only a directory spelled exactly as a level counts as that level's; `kutnohorsko_old` names none and is neutral.
            bool named = false;
            for (const std::filesystem::path &part : relative.parent_path()) {
                const std::string name = Lowercase(part);
                if (name == level) {
                    named = true;
                }
                else if (std::find(kLevelNames.begin(), kLevelNames.end(), name) != kLevelNames.end()) {
                    return std::nullopt;
                }
            }
            if (named) {
                return 0;
            }
            return relative.parent_path().empty() ? 2 : 1;
        }
    } // namespace

    std::vector<std::filesystem::path> FindNavmeshPaks(const std::string &source, const std::string &level, const std::filesystem::path &serverDirectory) {
        std::vector<std::filesystem::path> paks;
        if (!source.empty()) {
            const std::filesystem::path root = std::filesystem::path(source).is_absolute() ? std::filesystem::path(source) : serverDirectory / source;
            for (const std::filesystem::path &candidate : {root, root / "recast.pak", root / level / "recast.pak", root / "Levels" / level / "recast.pak", root / "Data" / "Levels" / level / "recast.pak"}) {
                AddPak(paks, candidate);
            }
        }

        const std::string wanted = Lowercase(level);
        std::error_code error;
        const std::filesystem::path files = serverDirectory / "files";
        std::vector<std::pair<int, std::filesystem::path>> discovered;
        std::filesystem::recursive_directory_iterator it(files, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            const std::filesystem::path &path = it->path();
            if (Lowercase(path.filename()) == "recast.pak" && it->is_regular_file(error)) {
                if (const std::optional<int> rank = Rank(path.lexically_relative(files), wanted)) {
                    discovered.emplace_back(*rank, path);
                }
            }
            it.increment(error);
        }
        std::sort(discovered.begin(), discovered.end());
        for (const auto &[rank, pak] : discovered) {
            AddPak(paks, pak);
        }
        return paks;
    }
} // namespace KCDC::Features::Navigation
