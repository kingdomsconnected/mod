#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct BuffRuleDefinition {
        std::string_view guid;
        std::string_view implementation;
        std::string_view parameters;
        bool parametersAuthored;
    };
    struct PerkBuffRule {
        std::string_view perk;
        std::string_view buff;
    };
    struct PerkBuffRemap {
        std::string_view perk;
        std::string_view source;
        std::string_view target;
    };
    struct ItemBuffRule {
        std::string_view item;
        std::string_view buff;
    };
    extern const std::string_view g_BuffRuleCatalogHash;
    extern const std::vector<BuffRuleDefinition> g_BuffRules;
    extern const std::vector<PerkBuffRule> g_PerkBuffRules;
    extern const std::vector<PerkBuffRemap> g_PerkBuffRemaps;
    extern const std::vector<ItemBuffRule> g_ItemBuffRules;
    const BuffRuleDefinition *FindBuffRule(std::string_view guid);
    const BuffRuleDefinition *FindCustomBuffRule(std::string_view guid);
} // namespace KCDC::Shared::Game
