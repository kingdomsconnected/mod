#include "c_signal.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::shared {
    void C_Signal::EmitBool(bool value) {
        hook::this_call<void>(gPatterns.C_Signal__EmitBool, this, value);
    }
} // namespace SDK::wh::shared
