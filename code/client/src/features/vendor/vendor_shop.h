#pragma once

#include "features/items/native_inventory_projection.h"
#include "shared/features/vendor/vendor_rpc.h"

#include "sdk/types.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/shopmodule/c_shop.h"
#include "sdk/wh/shopmodule/c_shopping_transaction.h"

#include <cstdint>
#include <vector>

namespace KCDC::Features::Vendor {
    /**
     * The game's own shop, stood up for one trading session.
     *
     * The trade screen needs no shopkeeper NPC, only a `C_Shop`: a `Shop`
     * entity is spawned for the session, the keeper's soul is added to it, and
     * its stock inventory is filled straight from the server's list -- the
     * shop builds that inventory itself at init, so nothing is created on the
     * keeper's body and nothing has to be stripped off it afterwards. The
     * entity goes with the session, and its inventory goes with it.
     *
     * The DB row is `test_keys` (789), which has no item filters and an empty
     * preset: the filters would only decide multipliers the price detour
     * replaces, and an empty preset is what keeps the init and any restock
     * from putting the game's own goods on the shelf.
     *
     * Game-thread only: every method spawns, fills or opens something native.
     */
    class VendorShop final {
      public:
        /** Spawns the shop and keeps it for `keeperSoulWuid`. False when the engine refused; nothing is left behind. */
        bool Build(SDK::wh::framework::WUID keeperSoulWuid, const SDK::Vec3 &position);

        /**
         * Closes the deal and removes the entity, and with it the stock
         * inventory. The deal is closed first: it outlives the shop in the
         * shop module's map, and a basket still holding shelf items would
         * point into an inventory that is gone.
         */
        void Destroy();

        /** Lets go of the shop without touching it, for a world that has already taken it away. */
        void Forget();

        /**
         * Makes the shelf match the server's list and the purse.
         *
         * A diff rather than a refill, so what the screen shows does not flash
         * empty. False, and nothing touched, while the trade basket holds
         * anything: the basket keeps items by pointer and
         * `C_Inventory::RemoveItemsByClass` matches on the owner alone, so a
         * shelf edited under a full basket could delete an item the basket
         * still points at.
         */
        bool Restock(const std::vector<Shared::RPC::VendorStockRow> &stock, std::uint32_t purse);

        /** Brings the trade screen up with the keeper as the trader, and takes hold of its deal. */
        void Open();

        /**
         * Binds the deal to the player and the shop again if a trade closed it.
         *
         * `Accept` closes the transaction after every trade, which zeroes both
         * parties, and only the game re-opening the shop would bind them again.
         * A second Trade on the same screen would otherwise reach the commit
         * with no shop to compare against.
         */
        void Rebind();

        SDK::wh::shopmodule::C_ShoppingTransaction *GetTransaction() const {
            return _transaction;
        }

        SDK::wh::shopmodule::C_Shop *GetShop() const {
            return _shop;
        }

      private:
        SDK::wh::shopmodule::C_Shop *_shop                       = nullptr;
        SDK::wh::shopmodule::C_ShoppingTransaction *_transaction = nullptr;
        SDK::EntityId _entityId                                  = 0;
        SDK::wh::framework::WUID _keeper {};
        std::uint32_t _nextOrdinal = 1;
        KCDC::Features::Items::NativeInventoryProjection _stockProjection;
    };
} // namespace KCDC::Features::Vendor
