#pragma once

#include "shared/game/levels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace KCDC::Features::Navigation {
    /**
     * The level directories under the game's `Data/Levels` a server can run:
     * the `level` field of the server config takes its allowed values from
     * here, and discovery reads a directory with one of these names as
     * belonging to that level.
     */
    inline const std::vector<std::string> kLevelNames = [] {
        std::vector<std::string> names;
        for (const Shared::Game::PlayableLevel &level : Shared::Game::kPlayableLevels) {
            names.emplace_back(level.name);
        }
        return names;
    }();

    /**
     * Configured locations first, then recast.pak copies below the server's
     * files directory: those under a directory named for the level, then
     * those under no level's name, and a bare `files/recast.pak` last, each
     * group in path order. Copies under another level's directory are
     * skipped. Names are matched without regard to case.
     */
    std::vector<std::filesystem::path> FindNavmeshPaks(const std::string &source, const std::string &level, const std::filesystem::path &serverDirectory);
} // namespace KCDC::Features::Navigation
