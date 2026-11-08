#pragma once

#include <array>
#include <string_view>

namespace KCDC::Shared::Game {
    /** A level a session can run: its directory under the game's `Data/Levels`, the DLC that ships it (empty for the base game), and the region's name as a player reads it. */
    struct PlayableLevel {
        std::string_view name;
        std::string_view dlc;
        std::string_view displayName;
    };

    inline constexpr std::array<PlayableLevel, 3> kPlayableLevels {{
        {"kutnohorsko", "", "Kuttenberg"},
        {"trosecko", "", "Trosky"},
        {"klaster", "MysteriaEcclesiae", "Sedletz Monastery"},
    }};

    /** The row named `name`, or null for a level no session can run. */
    inline const PlayableLevel *FindPlayableLevel(std::string_view name) {
        for (const PlayableLevel &level : kPlayableLevels) {
            if (level.name == name) {
                return &level;
            }
        }
        return nullptr;
    }
} // namespace KCDC::Shared::Game
