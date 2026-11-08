#pragma once

#include "../entitymodule/c_item.h"
#include "../entitymodule/c_item_collection.h"
#include "../framework/wuid.h"
#include "i_shop.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shopmodule {
    /**
     * The part of a shop that is not an entity: the keeper, trade-screen and
     * stock contracts, and the haggle flag.
     *
     * The base constructor at 0x180D11C84 and RTTI establish `I_Shop` at +0x00
     * and `C_ItemCollection` at +0x08, then write the one member, the byte at
     * +0x78, which pads the class to 0x80. Its abstract table at 0x183A948F0
     * has 13 slots: slots 1-9 pure, slots 10-12 implemented here.
     */
    class C_ShopBase
        : public I_Shop
        , public entitymodule::C_ItemCollection {
      public:
        ~C_ShopBase() override = default;

        virtual void unk_shop_base_fn0003() = 0;
        virtual void unk_shop_base_fn0004() = 0;

        /** Slot 5 (`C_Shop`: 0x1813C51BC): whether a soul keeps this shop. */
        virtual bool IsKeeper(const framework::WUID *soulWuid) const = 0;

        /** Slot 6 (`C_Shop`: 0x180D445C0): appends a soul to the keeper list. */
        virtual void AddKeeper(const framework::WUID *soulWuid) = 0;

        /**
         * Slot 7 (`C_Shop`: 0x180C406D8): opens the game's trade screen on this
         * shop, with `keeperSoulWuid` as the trader.
         *
         * Builds the screen's payload, creates the transaction keyed by this
         * shop's WUID in the shop module, and hands the payload to the
         * `I_UIShop` at `C_GlobalEnvironment+0x1B8`. The Lua binding
         * `Shop.OpenInventory(keeperEntity, haggle)` is `SetHaggleAllowed`
         * followed by this.
         */
        virtual void OpenForKeeper(framework::WUID keeperSoulWuid) = 0;

        /**
         * Slot 8 (`C_Shop`: 0x1819B8228): moves `count` units of `item` into
         * the stock inventory through `C_Inventory::AddItem`. The transaction's
         * own sell path, so the item is whatever the player handed over, not a
         * fresh one.
         */
        virtual entitymodule::C_Item *AddItemToStock(entitymodule::C_Item *item, std::uint32_t count) = 0;

        virtual void unk_shop_base_fn0009() = 0;

        /** Slot 10 (0x181A7D970): writes `m_bHaggleAllowed`, which the haggle button reads. */
        virtual void SetHaggleAllowed(bool allowed) = 0;

        /** Slot 11 (0x1808C55E4); reads the haggle flag, but what else it weighs is not proven. */
        virtual void unk_shop_base_fn0011() = 0;
        virtual void unk_shop_base_fn0012() = 0;

      private:
        bool m_bHaggleAllowed;        // 0x78
        std::byte m_padding0079[0x7]; // 0x79

        static void AssertLayout() {
            static_assert(offsetof(C_ShopBase, m_bHaggleAllowed) == 0x78, "C_ShopBase::m_bHaggleAllowed offset mismatch");
        }
    };

    static_assert(sizeof(C_ShopBase) == 0x80, "C_ShopBase size mismatch");
} // namespace SDK::wh::shopmodule
