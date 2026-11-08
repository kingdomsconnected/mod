#pragma once

#include "c_edge_port.h"

namespace SDK::wh::conceptmodule {
    /** An edge port carrying a value rather than a trigger. Adds no storage. */
    class C_DataPort: public C_EdgePort {};

    static_assert(sizeof(C_DataPort) == 0x30, "C_DataPort size mismatch");
} // namespace SDK::wh::conceptmodule
