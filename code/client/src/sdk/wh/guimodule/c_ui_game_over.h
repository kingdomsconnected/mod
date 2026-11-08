#pragma once

#include "c_ui_flash_base.h"

namespace SDK::wh::guimodule {
    /**
     * The death screen (RTTI 0x184134640), a Flash page like any other: its
     * element is the `C_UIFlashBase` one at +0x48, and it implements
     * `playermodule::I_UIGameOver` on a sub-object at +0x58.
     *
     * `ShowPicture` sets that element visible and nothing in the game sets it
     * back, because the shipped game leaves the screen by loading a level.
     */
    class C_UIGameOver final: public C_UIFlashBase {
      public:
        ~C_UIGameOver() override = default;
    };
} // namespace SDK::wh::guimodule
