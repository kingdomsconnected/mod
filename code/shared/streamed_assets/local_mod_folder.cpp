#include "local_mod_folder.h"

#include "lua_script_scan.h"
#include "streamed_asset_policy.h"

#include <utils/crypto.h>
#include <utils/streamed_assets/pak_archive.h>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace KCDC::Shared::StreamedAssets {
    namespace {
        const std::string kLuaExtension = ".lua";

        bool BuildLane(const std::filesystem::path &laneRoot, const std::string &resource, const std::string &lane, const std::filesystem::path &cache, LocalModPak &out, std::string &error) {
            const Framework::Utils::StreamedAssets::Policy &policy = *Policy::Get();
            std::error_code code;
            std::vector<Framework::Utils::StreamedAssets::PakSourceEntry> entries;
            for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(laneRoot, code)) {
                if (!entry.is_regular_file(code)) {
                    continue;
                }
                const std::string relative = std::filesystem::relative(entry.path(), laneRoot, code).generic_string();
                std::string normalized;
                std::string reason;
                if (!policy.NormalizeEntry(lane, resource, relative, normalized, reason)) {
                    error = "'" + resource + "' " + lane + ": " + relative + ": " + reason;
                    return false;
                }
                if (policy.ShouldShip(lane, resource, normalized, entry.path().string(), reason)) {
                    entries.push_back({normalized, entry.path().string()});
                }
            }
            if (code || entries.empty()) {
                error = "'" + resource + "' " + lane + " has no file a mod may ship";
                return false;
            }

            std::sort(entries.begin(), entries.end(), [](const Framework::Utils::StreamedAssets::PakSourceEntry &a, const Framework::Utils::StreamedAssets::PakSourceEntry &b) { return a.path < b.path; });
            std::string fingerprint;
            for (const Framework::Utils::StreamedAssets::PakSourceEntry &entry : entries) {
                fingerprint += fmt::format("{}|{}|{}\n", entry.path, std::filesystem::file_size(entry.sourceFile, code), std::filesystem::last_write_time(entry.sourceFile, code).time_since_epoch().count());
            }
            const std::string fingerprintHash = Framework::Utils::Crypto::Sha256Hex(fingerprint);

            const auto adopt = [&](const std::string &sha256) {
                const std::filesystem::path path = cache / Framework::Utils::StreamedAssets::PakFileName(resource, lane, sha256);
                out                              = {lane, sha256, path.string(), std::filesystem::file_size(path, code)};
                return !code;
            };

            // An unchanged lane keeps its pak: a level edit is a gigabyte to rewrite and hash.
            const std::filesystem::path indexPath = cache / (resource + "." + lane + ".index.json");
            if (std::ifstream index(indexPath); index) {
                const nlohmann::json cached = nlohmann::json::parse(index, nullptr, false);
                if (cached.is_object() && cached.value("fingerprint", "") == fingerprintHash) {
                    const std::string sha256 = cached.value("sha256", "");
                    if (Framework::Utils::StreamedAssets::IsSha256(sha256) && std::filesystem::is_regular_file(cache / Framework::Utils::StreamedAssets::PakFileName(resource, lane, sha256), code)) {
                        return adopt(sha256);
                    }
                }
            }

            const std::filesystem::path building = cache / (resource + "." + lane + ".building");
            if (!Framework::Utils::StreamedAssets::PakArchive::Write(building.string(), entries, policy, error)) {
                error = "'" + resource + "' " + lane + ": " + error;
                std::filesystem::remove(building, code);
                return false;
            }
            const std::string sha256 = Framework::Utils::Crypto::Sha256FileHex(building.string());
            if (sha256.empty()) {
                error = "'" + resource + "' " + lane + ": cannot hash the built pak";
                std::filesystem::remove(building, code);
                return false;
            }

            // A pak a running session may still hold stays until it is unused; only its own lane's go.
            const std::string prefix = resource + "." + lane + ".";
            for (const std::filesystem::directory_entry &staged : std::filesystem::directory_iterator(cache, code)) {
                const std::string name = staged.path().filename().string();
                if (name.rfind(prefix, 0) == 0 && staged.path().extension() == ".pak") {
                    std::filesystem::remove(staged.path(), code);
                }
            }
            code.clear();
            const std::filesystem::path path = cache / Framework::Utils::StreamedAssets::PakFileName(resource, lane, sha256);
            std::filesystem::rename(building, path, code);
            if (code) {
                error = "'" + resource + "' " + lane + ": cannot move the built pak into place";
                return false;
            }
            std::ofstream(indexPath, std::ios::trunc) << nlohmann::json {{"fingerprint", fingerprintHash}, {"sha256", sha256}}.dump();
            return adopt(sha256);
        }
    } // namespace

    std::vector<LocalMod> ListLocalMods(const std::filesystem::path &resources, std::string &error) {
        error.clear();
        std::vector<LocalMod> mods;
        std::error_code code;
        for (std::filesystem::directory_iterator folder(resources, code), end; !code && folder != end; folder.increment(code)) {
            std::error_code entryCode;
            const std::string name = folder->path().filename().string();
            if (!folder->is_directory(entryCode) || !Framework::Utils::StreamedAssets::IsValidResourceName(name)) {
                continue;
            }
            LocalMod mod;
            mod.resource = name;
            std::set<std::string> risky;
            for (const std::string &lane : Policy::Get()->GetLanes()) {
                const std::filesystem::path laneRoot = folder->path() / lane;
                if (!std::filesystem::is_directory(laneRoot, entryCode)) {
                    continue;
                }
                mod.lanes.push_back(lane);
                for (const std::filesystem::directory_entry &file : std::filesystem::recursive_directory_iterator(laneRoot, entryCode)) {
                    if (!file.is_regular_file(entryCode)) {
                        continue;
                    }
                    mod.bytes += file.file_size(entryCode);
                    if (lane == kLaneScript && file.path().extension() == kLuaExtension) {
                        std::ifstream stream(file.path(), std::ios::binary);
                        const std::string text {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
                        const LuaScriptScan scan = ScanLuaScript(text);
                        risky.insert(scan.risky.begin(), scan.risky.end());
                    }
                }
            }
            if (mod.lanes.empty()) {
                continue;
            }
            mod.risky.assign(risky.begin(), risky.end());
            if (std::ifstream package(folder->path() / "package.json"); package) {
                const nlohmann::json manifest = nlohmann::json::parse(package, nullptr, false);
                if (manifest.is_object()) {
                    mod.version     = manifest.value("version", "");
                    mod.description = manifest.value("description", "");
                }
            }
            mods.push_back(std::move(mod));
        }
        if (code) {
            error = "This folder cannot be read. Check that it exists and that you have access.";
            return {};
        }
        std::sort(mods.begin(), mods.end(), [](const LocalMod &a, const LocalMod &b) { return a.resource < b.resource; });
        return mods;
    }

    bool BuildLocalMod(const std::filesystem::path &resources, const std::string &resource, const std::filesystem::path &cache, std::vector<LocalModPak> &paks, std::string &error) {
        std::error_code code;
        std::filesystem::create_directories(cache, code);
        if (code) {
            error = "Cannot create the local mod cache";
            return false;
        }
        const std::filesystem::path folder = resources / resource;
        for (const std::string &lane : Policy::Get()->GetLanes()) {
            const std::filesystem::path laneRoot = folder / lane;
            if (!std::filesystem::is_directory(laneRoot, code)) {
                continue;
            }
            LocalModPak pak;
            if (!BuildLane(laneRoot, resource, lane, cache, pak, error)) {
                return false;
            }
            paks.push_back(std::move(pak));
        }
        return true;
    }
} // namespace KCDC::Shared::StreamedAssets
