#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct SoulRuleParameter {
        std::string_view name;
        float value;
    };
    struct SoulPerkAbility {
        std::string_view perk;
        std::uint32_t ability;
    };
    extern const std::string_view g_SoulRuleCatalogHash;
    extern const std::vector<SoulRuleParameter> g_SoulRuleParameters;
    extern const std::vector<SoulPerkAbility> g_SoulPerkAbilities;
} // namespace KCDC::Shared::Game
