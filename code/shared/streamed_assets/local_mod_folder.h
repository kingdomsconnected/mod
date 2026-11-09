#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KCDC::Shared::StreamedAssets {
    /** A client mod found in a server's resources folder. */
    struct LocalMod {
        std::string resource;
        std::string version;
        std::string description;
        std::vector<std::string> lanes;
        std::uint64_t bytes = 0;
        /** What its scripts ask for; what ticking it accepts. */
        std::vector<std::string> risky;
    };

    /**
     * The client mods in a resources folder: every folder holding at least one asset lane. A
     * gamemode or a world resource has none and is left out. `error` is set when the folder cannot
     * be read.
     */
    std::vector<LocalMod> ListLocalMods(const std::filesystem::path &resources, std::string &error);

    /** A pak built from one lane of a local mod. */
    struct LocalModPak {
        std::string lane;
        std::string sha256;
        std::string path;
        std::uint64_t size = 0;
    };

    /**
     * Builds the paks of one resource into `cache`, held to the same policy a server's are, and
     * reuses them while the lane is unchanged. The offline World Builder has no server to build
     * them. Files only, so safe on a worker thread.
     */
    bool BuildLocalMod(const std::filesystem::path &resources, const std::string &resource, const std::filesystem::path &cache, std::vector<LocalModPak> &paks, std::string &error);
} // namespace KCDC::Shared::StreamedAssets
