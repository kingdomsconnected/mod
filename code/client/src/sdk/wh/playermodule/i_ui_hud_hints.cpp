#include "i_ui_hud_hints.h"

#include <cstdint>

namespace SDK::wh::playermodule {
    I_UIHudHints *I_UIHudHints::GetInstance() {
        const auto environment =
            *reinterpret_cast<std::uint8_t **>(gPatterns.WHGlobalEnvironment__Instance);
        if (environment == nullptr) {
            return nullptr;
        }

        const auto playerModule =
            *reinterpret_cast<std::uint8_t **>(environment + 0x128);
        if (playerModule == nullptr) {
            return nullptr;
        }

        const auto playerUiOwner =
            *reinterpret_cast<std::uint8_t **>(playerModule + 0x58);
        if (playerUiOwner == nullptr) {
            return nullptr;
        }

        return *reinterpret_cast<I_UIHudHints **>(playerUiOwner + 0x130);
    }
} // namespace SDK::wh::playermodule
