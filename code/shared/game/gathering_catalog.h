#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct GatheringKind {
        std::uint8_t id;
        std::string_view itemClass;
        std::uint32_t amount;
        float respawnHours;
    };

    extern const std::vector<GatheringKind> g_GatheringKinds;
    const GatheringKind *FindGatheringKind(std::uint8_t kind);
} // namespace KCDC::Shared::Game
