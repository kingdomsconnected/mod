#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    /**
     * The game's effect that takes the cart dismount key away from the local
     * player while it is active (`C_DisablePlayerCartDismount`, RTTR).
     */
    class C_DisablePlayerCartDismount {
      public:
        /**
         * Its activation (0x18295D79C): raises or lowers the local player's
         * `C_CartPlayerControl::m_dismountDisabled` count and, while seated,
         * re-applies the `cart` action map that carries the key
         * (`UpdateCartActionMap`, 0x1828F2DC8) -- the key and its hint are
         * offered only at zero. It reaches the control through the local
         * player and never reads `this`, so it is called without an effect.
         */
        static void SetDismountDisabled(bool disabled) {
            hook::this_call<void>(gPatterns.C_DisablePlayerCartDismount__SetDismountDisabled, nullptr, disabled);
        }
    };
} // namespace SDK::wh::entitymodule
