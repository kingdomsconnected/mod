#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    /**
     * The game's trade screen.
     *
     * Reached through the `framework::I_UIShop` the global environment holds
     * at +0x1B8, which is this object's subobject at +0x10; that interface's
     * slot 11 hands back the object itself. Nothing here reads a member, so
     * none is mapped and the size is not asserted.
     *
     * Whether the screen is up is not read off this object either: opening it
     * blocks `E_UIState::Shop` on `C_UIStateBlockers` (0x1808C2A60) and
     * closing it unblocks the same state, so `IsHeld(Shop)` is the flag.
     */
    class C_UIShop final {
      public:
        /**
         * Takes the trade screen down (0x1808C2360): unblocks
         * `E_UIState::Shop`, tells the HUD, and drops the screen's transaction
         * pointer. The same path the screen's own close action runs.
         */
        void Close() {
            hook::this_call<void>(gPatterns.C_UIShop__Close, this);
        }
    };
} // namespace SDK::wh::guimodule
