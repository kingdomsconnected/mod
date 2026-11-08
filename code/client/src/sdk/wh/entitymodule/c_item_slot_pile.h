#pragma once

#include "c_item_slot.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Pile-flavoured spawn slot.
     *
     * Factory path 0x18111A120 allocates exactly 0xE0 bytes, constructs the
     * C_ItemSlot base, and replaces only its four inherited vtable pointers.
     * No pile-specific storage is present.
     */
    class C_ItemSlotPile : public C_ItemSlot {};

    static_assert(sizeof(C_ItemSlotPile) == 0xE0,
                  "C_ItemSlotPile size mismatch");
} // namespace SDK::wh::entitymodule
