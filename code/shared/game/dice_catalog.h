#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct DiceTable {
        std::string_view level;
        std::uint64_t guid;
        std::uint64_t firstSeatGuid;
        std::uint64_t secondSeatGuid;
        float x, y, z;
    };

    extern const std::vector<DiceTable> g_DiceTables;
} // namespace KCDC::Shared::Game
