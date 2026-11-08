#pragma once

#include "c_item_slot.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Item slot accepting multiple item classes.
     *
     * Factory path 0x181EEDEC0 allocates exactly 0xE0 bytes, constructs the
     * C_ItemSlot base, and replaces only its four inherited vtable pointers.
     * The reflected GetMultiSlotItem operation therefore reads the common
     * C_ItemSlot item pointer at +0x98.
     */
    class C_ItemSlotMultiClass : public C_ItemSlot {};

    static_assert(sizeof(C_ItemSlotMultiClass) == 0xE0,
                  "C_ItemSlotMultiClass size mismatch");
} // namespace SDK::wh::entitymodule
