#pragma once

#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct CustomBuffModifierName {
        std::string_view name;
        std::string_view native;
    };
    extern const std::vector<CustomBuffModifierName> g_CustomBuffModifiers;
} // namespace KCDC::Shared::Game
