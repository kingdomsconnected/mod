#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct SoulNumericDefinition {
        std::uint32_t id;
        std::string_view name;
    };
    struct SoulPerkDefinition {
        std::string_view id;
        std::string_view name;
    };
    /** Identities only. Levels, XP and ownership have no inferred defaults. */
    extern const std::vector<SoulNumericDefinition> g_SoulSkills;
    extern const std::vector<SoulNumericDefinition> g_SoulStats;
    extern const std::vector<SoulPerkDefinition> g_SoulPerks;
    extern const std::string_view g_SoulProfileCatalogHash;
} // namespace KCDC::Shared::Game
