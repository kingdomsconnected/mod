#include "c_tutorial_manager.h"

#include <cstdint>

namespace SDK::wh::playermodule {
    C_TutorialManager *C_TutorialManager::GetInstance() {
        const auto environment =
            *reinterpret_cast<std::uint8_t **>(gPatterns.WHGlobalEnvironment__Instance);
        if (environment == nullptr) {
            return nullptr;
        }

        const auto playerModule = *reinterpret_cast<std::uint8_t **>(environment + 0x128);
        if (playerModule == nullptr) {
            return nullptr;
        }

        return *reinterpret_cast<C_TutorialManager **>(playerModule + 0x20);
    }
} // namespace SDK::wh::playermodule
