#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct NpcPlacement {
        std::string_view level;
        std::string_view name;
        std::string_view guid;
        std::string_view actorClass;
        std::string_view soul;
        std::string_view layer;
        float x, y, z;
        float qw, qx, qy, qz;
    };

    extern const std::vector<NpcPlacement> g_NpcPlacements;
} // namespace KCDC::Shared::Game
