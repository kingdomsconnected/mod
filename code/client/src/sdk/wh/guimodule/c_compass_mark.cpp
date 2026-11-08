#include "c_compass_mark.h"

#include "../../patterns.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    void C_CompassMark::SetState(std::int32_t state) {
        hook::this_call<void>(gPatterns.C_CompassMark__SetState, this, state);
    }

    CompassMarkRef MakeCompassMark(E_CompassMarkType type, const Vec3 &position, std::int32_t state) {
        // A free function returning a shared_ptr by value: the hidden return
        // pointer is its first argument, and all three inputs travel by const
        // reference, so every scalar needs storage that outlives the call.
        const std::int32_t typeStorage  = static_cast<std::int32_t>(type);
        const std::int32_t stateStorage = state;

        CompassMarkRef mark;
        hook::call<void>(gPatterns.MakeCompassMark, mark.Slot(), &typeStorage, &position, &stateStorage);
        return mark;
    }
} // namespace SDK::wh::guimodule
