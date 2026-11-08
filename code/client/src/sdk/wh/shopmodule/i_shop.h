#pragma once

#include "../rpgmodule/i_rpg_shop.h"

namespace SDK::wh::shopmodule {
    /**
     * The shop module's interface, between `I_RPGShop` and `C_ShopBase`.
     *
     * RTTI places it at +0x00 of `C_ShopBase`, sharing the primary table, and
     * the binary emits no table of its own. Which of `C_ShopBase`'s slots 3-9
     * it declares is therefore not provable, and they are declared on
     * `C_ShopBase`, the first table they are seen in.
     */
    class I_Shop: public rpgmodule::I_RPGShop {
      public:
        ~I_Shop() override = default;
    };

    static_assert(sizeof(I_Shop) == 0x8, "I_Shop size mismatch");
} // namespace SDK::wh::shopmodule
