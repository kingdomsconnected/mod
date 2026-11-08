#pragma once

#include "i_item_descriptor.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Empty RTTI-proven intermediate descriptor base. */
    class C_HomogenousItemDescriptor : public I_ItemDescriptor {
      protected:
        ~C_HomogenousItemDescriptor() override = default;
    };

    static_assert(sizeof(C_HomogenousItemDescriptor) == 0x8,
                  "C_HomogenousItemDescriptor size mismatch");
} // namespace SDK::wh::entitymodule
