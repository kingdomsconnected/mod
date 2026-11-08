#pragma once

#include "core/feature_registry.h"

#include "vendor_shop.h"

#include "shared/features/vendor/vendor_rpc.h"

#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/shopmodule/c_shop.h"
#include "sdk/wh/shopmodule/c_shopping_transaction.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace KCDC::Features::Vendor {
    /**
     * The client half of vendors: the game's own trade screen, with the
     * server's prices on it and the server's hand on every deal.
     *
     * Three native seams carry it, and none of them trusts the screen:
     *
     * - **Prices.** `C_RPGInventory::GetShopItemPrice` is the only place the
     *   screen asks what anything costs, for either side of the counter. For
     *   this session's shop it answers from the server's lists: the stock
     *   price on the vendor's side, the buy price on the player's, and nothing
     *   for what the vendor does not buy.
     *
     * - **The deal.** `C_ShoppingTransaction::ApplyTransaction` is where every
     *   completed trade moves its money and items. For this session's shop it
     *   never runs: the basket goes to the server instead, and `Accept`, which
     *   called it, then closes the transaction as usual -- which is what hands
     *   every basket item back to where it came from. Nothing moves until the
     *   server's grants and takes arrive.
     *
     * - **What the vendor takes.** `GetShopSaleRefusal` is the filter the
     *   screen keeps on the player's column. Anything the vendor does not buy
     *   is refused there, which is what greys it out.
     *
     * - **Closing.** The screen blocks `E_UIState::Shop` while it is up, so
     *   the block going away is the player leaving, and is reported.
     *
     * One more path is shut rather than rerouted: `Shop.AcceptTransaction`
     * falls back to `TransferMoneyWithKeeper` when it finds no open deal,
     * which pays the keeper out of the player's real purse with no commit at
     * all. Nothing in a session may move money that way.
     *
     * The price and the commit key on this session's own shop and deal, so any
     * other shop falls straight through to the game.
     *
     * Game-thread only: the detours and the tick both run there.
     */
    class VendorModule final: public KCDC::Core::IFeatureService {
      public:
        static VendorModule &Get();

        /** Installs the price and commit detours. Called once, at boot. */
        static void Install();

        void Update() override;
        void Reset() override;

        /**
         * The level went, and the shop entity with it. Raised on the loading
         * thread, so this only flags it; the tick lets go of the shop without
         * touching it and tells the server the session is over.
         */
        void NativeContextChanged() override;

        /** The server's price for one unit of `item` at `shop`, or nullopt to leave the game's own. */
        std::optional<std::int32_t> PriceFor(const SDK::wh::shopmodule::C_Shop *shop, SDK::wh::entitymodule::C_Item *item, bool selling) const;

        /**
         * Turns a committed deal at this session's shop into a basket for the
         * server. True when the deal was ours, and the native commit must then
         * not run.
         */
        bool InterceptCommit(SDK::wh::shopmodule::C_ShoppingTransaction *transaction);

        /**
         * Whether the vendor refuses one of the player's items. Asked by the
         * filter the screen keeps on the player's column, after the game's own
         * reasons have passed it.
         */
        bool RefusesSale(const SDK::wh::entitymodule::C_Item *item) const;

        /** Whether the screen's Lua must be kept from moving money past the commit; see the detour. */
        bool BlocksDirectPayment() const {
            return _phase != Phase::Idle;
        }

      private:
        enum class Phase : std::uint8_t {
            Idle,
            /** Opened by the server; the lists have not arrived yet. */
            AwaitingStock,
            /** The screen was asked for and has not come up yet. */
            Opening,
            Open,
        };

        /**
         * Why a session ended, as this machine saw it. The server is told only
         * about the ones it did not cause, and scripts see every one that ends
         * a screen they were told had opened.
         */
        enum class CloseCause : std::uint8_t {
            /** The player closed the screen. Reported as Closed. */
            Player,
            /** The screen never came up. Reported as Unavailable; no script event, as none opened. */
            Failed,
            /** The level went, and the shop with it. Reported as Unavailable. */
            LevelChange,
            /** The server closed it. */
            Server,
            /** The server opened another vendor over it. */
            Replaced,
            /** The connection ended. */
            SessionOver,
        };

        void InstallRPCs();
        void OnOpen(const Shared::RPC::VendorOpen &message);
        void OnStock(const Shared::RPC::VendorStock &message);
        void OnResult(const Shared::RPC::VendorResult &message);

        /** Stands the shop up and opens the screen, once the keeper's soul can be read. */
        void Build();

        /** Takes the screen and the shop down, tells the server when it did not cause it, and tells scripts. */
        void Teardown(CloseCause cause);

        /** The `reason` scripts see, or null for a cause that can only end a screen that never opened. */
        static const char *ScriptReason(CloseCause cause);

        bool IsScreenUp() const;
        std::uint32_t ShelfPurse() const;
        bool Buys() const;
        const Shared::RPC::VendorStockRow *FindStock(const Shared::RPC::ItemClassId &itemClass) const;
        const Shared::RPC::VendorBuyRow *FindBuy(const Shared::RPC::ItemClassId &itemClass) const;

        VendorShop _shop;
        Phase _phase = Phase::Idle;

        std::uint32_t _sessionId    = 0;
        std::uint64_t _npcNetworkId = 0;
        std::uint8_t _flags         = 0;

        /** The server's lists as last sent, and the revision a basket echoes. */
        std::uint32_t _revision = 0;
        std::uint32_t _purse    = 0;
        std::vector<Shared::RPC::VendorStockRow> _stock;
        std::vector<Shared::RPC::VendorBuyRow> _buys;
        bool _haveStock = false;

        /** A new list arrived while the screen was up; applied once the basket lets go of the shelf. */
        bool _restockPending = false;

        /** When the current phase gives up waiting. */
        MafiaNet::Time _deadline = 0;

        /** Consecutive frames the screen has been down while open; see Update(). */
        std::uint8_t _closedFrames = 0;

        /** Whether scripts were told the screen opened, so the close they hear always pairs with it. */
        bool _announced = false;

        bool _rpcsInstalled = false;

        /** Set from NativeContextChanged, acted on by the tick. */
        std::atomic<bool> _nativeContextLost = false;
    };
} // namespace KCDC::Features::Vendor
