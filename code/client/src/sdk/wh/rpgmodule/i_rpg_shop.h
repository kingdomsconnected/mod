#pragma once

namespace SDK::wh::rpgmodule {
    /**
     * The RPG rules' view of a shop: what `C_RPGInventory::GetShopItemPrice`
     * (0x180566410) takes as its shop argument.
     *
     * The abstract table at 0x183A94808 has three slots: the scalar deleting
     * destructor and two pure operations, which `C_Shop` fills with the buy
     * and sell price multipliers its DB row filters give an item class.
     */
    class I_RPGShop {
      public:
        virtual ~I_RPGShop() = default; // slot 0

        virtual void unk_rpg_shop_fn0001() = 0;
        virtual void unk_rpg_shop_fn0002() = 0;
    };

    static_assert(sizeof(I_RPGShop) == 0x8, "I_RPGShop size mismatch");
} // namespace SDK::wh::rpgmodule
