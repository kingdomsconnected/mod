#include "c_ui_hud_messages.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK::wh::guimodule {
    C_UIHudMessages *C_UIHudMessages::GetInstance() {
        const auto environment =
            *reinterpret_cast<std::uint8_t **>(gPatterns.WHGlobalEnvironment__Instance);
        if (environment == nullptr) {
            return nullptr;
        }

        auto *guiModule = *reinterpret_cast<void **>(environment + 0xE8);
        if (guiModule == nullptr) {
            return nullptr;
        }

        return hook::call<C_UIHudMessages *>(gPatterns.C_UIHudMessages__GetInstance, guiModule);
    }
} // namespace SDK::wh::guimodule
