#pragma once

#include "c_data_port.h"

namespace SDK::wh::conceptmodule {
    /** A data port a node produces rather than consumes. Adds no storage. */
    class C_OutputDataPort: public C_DataPort {};

    static_assert(sizeof(C_OutputDataPort) == 0x30, "C_OutputDataPort size mismatch");
} // namespace SDK::wh::conceptmodule
