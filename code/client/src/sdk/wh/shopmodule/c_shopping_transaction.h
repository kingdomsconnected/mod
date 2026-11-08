#pragma once

#include "../../patterns.h"

#include "../entitymodule/c_inventory.h"
#include "../entitymodule/c_item.h"
#include "c_shop.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shopmodule {
    /**
     * One deal on the trade screen: who is buying, from which shop, and the
     * items dragged into the basket so far.
     *
     * Created at 0xC8 by `C_ShopModule::GetOrCreateTransaction` (0x180C47C10),
     * keyed by the shop's WUID and kept in the module's map for good. Its
     * interface `I_ShopTransaction` (vtable 0x183D157F0) has 11 slots; slot 10
     * is `ApplyTransaction`, the single point every completed trade goes
     * through, reached only from `Accept` (slot 3), which then calls `Close`.
     *
     * The parties are not fixed at creation. Slot 1 (`BindParties`,
     * 0x180C47D14) sets the customer inventory and the shop, and `Close`
     * zeroes both -- so after a trade the transaction points at no shop until
     * `GetOrCreateTransaction`, which runs slot 1 on the found path too, binds
     * it again. `Checkout` only sets the open flag.
     *
     * Basket items are split stacks parked in the indexer at +0x20. Each keeps
     * its owner, so an item is one the player is buying exactly when its owner
     * is the shop's stock inventory.
     */
    class C_ShoppingTransaction {
      public:
        entitymodule::C_Inventory *GetCustomerInventory() const {
            return m_pCustomerInventory;
        }

        /** The shop the deal is with. Null once `Close` has run, until the next bind. */
        C_Shop *GetShop() const {
            return m_pShop;
        }

        /** Set by `Checkout`, cleared by `Close`. `Shop.AcceptTransaction` only finds an open one. */
        bool IsOpen() const {
            return m_open;
        }

        /** Read in place: the vector is the game's, and copying it would free through the wrong heap. */
        std::size_t GetBasketCount() const {
            return m_pBasketBegin != nullptr ? static_cast<std::size_t>(m_pBasketEnd - m_pBasketBegin) : 0;
        }

        entitymodule::C_Item *GetBasketItemAt(std::size_t index) const {
            return index < GetBasketCount() ? m_pBasketBegin[index] : nullptr;
        }

        /**
         * Ends the deal (0x182048070): hands every basket item back to its
         * owner, zeroes both parties and clears the open flag. A closed
         * transaction closes again harmlessly.
         */
        void Close() {
            hook::this_call<void>(gPatterns.I_ShopTransaction__Close, this);
        }

      private:
        std::byte m_vtable0000[0x8];                     // 0x000, I_ShopTransaction
        std::uint64_t m_shopWuid;                        // 0x008, the key it was created under
        entitymodule::C_Inventory *m_pCustomerInventory; // 0x010
        C_Shop *m_pShop;                                 // 0x018
        std::byte m_basket0020[0x78];                    // 0x020, C_ItemIndexer
        entitymodule::C_Item **m_pBasketBegin;           // 0x098
        entitymodule::C_Item **m_pBasketEnd;             // 0x0A0
        entitymodule::C_Item **m_pBasketCapacity;        // 0x0A8
        std::int32_t m_frozenPrices00B0[3];              // 0x0B0, written by Checkout
        bool m_open;                                     // 0x0BC
        std::byte m_padding00BD[0x3];                    // 0x0BD
        std::uint64_t m_moneyFailure;                    // 0x0C0, the sentinel TransferMoney returns on a short purse

        static void AssertLayout() {
            static_assert(offsetof(C_ShoppingTransaction, m_shopWuid) == 0x08, "C_ShoppingTransaction::m_shopWuid offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_pCustomerInventory) == 0x10, "C_ShoppingTransaction::m_pCustomerInventory offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_pShop) == 0x18, "C_ShoppingTransaction::m_pShop offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_pBasketBegin) == 0x98, "C_ShoppingTransaction::m_pBasketBegin offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_pBasketEnd) == 0xA0, "C_ShoppingTransaction::m_pBasketEnd offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_open) == 0xBC, "C_ShoppingTransaction::m_open offset mismatch");
            static_assert(offsetof(C_ShoppingTransaction, m_moneyFailure) == 0xC0, "C_ShoppingTransaction::m_moneyFailure offset mismatch");
        }
    };

    static_assert(sizeof(C_ShoppingTransaction) == 0xC8, "C_ShoppingTransaction size mismatch");
} // namespace SDK::wh::shopmodule
