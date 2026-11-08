#pragma once

#include "../../patterns.h"
#include "../entitymodule/c_item.h"
#include "c_ui_inventory_base.h"
#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    /** Constructor 0x180BC8ABC calls C_UIInventoryBase at offset zero.
     * Embedded at C_UIApse+0xD8, ending at the player page at +0xBA0.
     */
    class C_UIApseInventory: public C_UIInventoryBase {
      public:
        void UseConsumableItem(SDK::wh::entitymodule::C_Item *item, bool saveConfirmed) {
            hook::this_call<void>(gPatterns.C_UIApseInventory__UseConsumableItem, this, item, saveConfirmed);
        }

      private:
        std::byte m_opaque05D0[0x4F8];
    };
    static_assert(sizeof(C_UIApseInventory) == 0xAC8);
} // namespace SDK::wh::guimodule
