#pragma once

#include "../guimodule/c_ui_shop.h"

namespace SDK::wh::framework {
    /**
     * The trade screen as the rest of the game sees it: what the global
     * environment holds at +0x1B8, and what `C_Shop::OpenForKeeper` opens.
     *
     * The interface's own vtable (0x183EF5550) has 13 slots, 1 to 9 pure.
     * `C_UIShop` implements it at +0x10 with the table at 0x183D2C688; only
     * the slots the mod calls are named.
     */
    class I_UIShop {
      public:
        /** Slot 0, the scalar deleting destructor. Never called from here. */
        virtual void unk_fn0000() = 0;

        /** Slot 1 (0x1808C2A60): opens the screen on a transaction payload and blocks `E_UIState::Shop`. */
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;

        /** Slot 11 (0x18213A288): the screen object this interface is part of, `this - 0x10`. */
        virtual guimodule::C_UIShop *GetUIShop() = 0;

        virtual void unk_fn0012() = 0;

      protected:
        ~I_UIShop() = default;
    };
} // namespace SDK::wh::framework
