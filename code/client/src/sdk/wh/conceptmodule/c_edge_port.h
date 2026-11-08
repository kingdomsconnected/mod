#pragma once

#include "i_port.h"

namespace SDK::wh::conceptmodule {
    /**
     * A port that can sit at the end of a graph edge. Adds no storage over
     * `I_Port`: `C_ConstantPort`'s value lands at 0x30, which is exactly
     * where `I_Port` ends, so every class between the two is empty.
     */
    class C_EdgePort: public I_Port {};

    static_assert(sizeof(C_EdgePort) == 0x30, "C_EdgePort size mismatch");
} // namespace SDK::wh::conceptmodule
