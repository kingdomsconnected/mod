#include "i_ui_hud_events.h"

#include "../../patterns.h"

#include <cstdint>

namespace SDK::wh::framework {
    I_UIHudEvents *I_UIHudEvents::GetInstance() {
        const auto environment =
            *reinterpret_cast<std::uint8_t **>(gPatterns.WHGlobalEnvironment__Instance);
        if (environment == nullptr) {
            return nullptr;
        }

        const auto entityModule =
            *reinterpret_cast<std::uint8_t **>(environment + 0xE0);
        if (entityModule == nullptr) {
            return nullptr;
        }

        return *reinterpret_cast<I_UIHudEvents **>(entityModule + 0x178);
    }
} // namespace SDK::wh::framework
