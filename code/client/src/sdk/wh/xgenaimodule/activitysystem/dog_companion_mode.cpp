#include "dog_companion_mode.h"

#include "../../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule::activitysystem {
    void SetPlayerDogMode(E_DogCompanionMode mode) {
        // A free function -- `YAX` in the mangled name -- so the mode is the
        // first argument and there is no `this`.
        hook::call<void>(gPatterns.SetPlayerDogMode, static_cast<std::uint8_t>(mode));
    }
} // namespace SDK::wh::xgenaimodule::activitysystem
