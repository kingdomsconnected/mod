#pragma once

#include "../entitymodule/c_inventory.h"

namespace SDK::wh::framework {
    /**
     * Native transfer screen interface at C_GlobalEnvironment+0x1C0.
     * RTTI places it at C_UIItemTransfer+0x58, table 0x183D2D6D8.
     * The actor's Lua OpenItemTransferStore builds its payload and calls slot
     * 1. Slots 2, 3 and 6 below are proven against that same concrete table.
     */
    class I_UIItemTransfer {
      public:
        virtual void unk_fn0000()                                                 = 0;
        virtual void unk_fn0001()                                                 = 0;
        virtual void Close(bool closeFlag)                                        = 0; // 0x180C424F4, bool forwarded to Flash
        virtual bool IsUsingInventory(entitymodule::C_Inventory *inventory) const = 0; // 0x182B8DDAC
        virtual void unk_fn0004()                                                 = 0;
        virtual void unk_fn0005()                                                 = 0;
        virtual bool IsOpen() const                                               = 0; // 0x182B8DD94, includes closing animation
        virtual void unk_fn0007()                                                 = 0;

      protected:
        ~I_UIItemTransfer() = default;
    };

    static_assert(sizeof(I_UIItemTransfer) == 0x8, "I_UIItemTransfer size mismatch");
} // namespace SDK::wh::framework
