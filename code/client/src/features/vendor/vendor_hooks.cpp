#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/vendor/vendor_module.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/rpgmodule/c_rpg_inventory.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/shopmodule/c_shop.h"
#include "sdk/wh/shopmodule/c_shopping_transaction.h"

#include <cstdint>

namespace KCDC::Features::Vendor {
    namespace {
        // The last argument is the shop's `I_RPGShop` face, which is `C_Shop`'s primary base.
        using GetShopItemPrice_t = std::int32_t *(__fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *, std::int32_t *, SDK::wh::entitymodule::C_Item *, SDK::wh::rpgmodule::I_Soul *, SDK::wh::rpgmodule::I_Soul *, std::int32_t, SDK::wh::shopmodule::C_Shop *);
        using ApplyTransaction_t = void(__fastcall *)(SDK::wh::shopmodule::C_ShoppingTransaction *, std::int32_t, const SDK::wh::framework::WUID *);

        // The first argument is unused by the native; the last is the shop's
        // S_ShopDBData row, which the SDK does not carry -- neither is read here.
        using GetShopSaleRefusal_t      = std::int32_t(__fastcall *)(const void *, const SDK::wh::entitymodule::C_Item *, const void *);
        using TransferMoneyWithKeeper_t = void(__fastcall *)(void *, SDK::wh::rpgmodule::I_Soul *, std::int32_t);

        GetShopItemPrice_t g_getShopItemPriceOriginal               = nullptr;
        ApplyTransaction_t g_applyTransactionOriginal               = nullptr;
        GetShopSaleRefusal_t g_getShopSaleRefusalOriginal           = nullptr;
        TransferMoneyWithKeeper_t g_transferMoneyWithKeeperOriginal = nullptr;

        /** The row filter's own code, and so what the screen already shows as "the trader does not buy this". */
        constexpr std::int32_t kRefusedByShop = 5;

        /**
         * Every price the trade screen shows, for either side of the counter.
         *
         * The game's own price is computed first and then overwritten, so the
         * out pointer is always the one the caller passed and the function's
         * side effects -- none are known, but the call stays -- still happen.
         */
        std::int32_t *__fastcall GetShopItemPrice_Hook(SDK::wh::rpgmodule::C_RPGInventory *self, std::int32_t *price, SDK::wh::entitymodule::C_Item *item, SDK::wh::rpgmodule::I_Soul *customer, SDK::wh::rpgmodule::I_Soul *keeper, std::int32_t selling,
            SDK::wh::shopmodule::C_Shop *shop) {
            std::int32_t *result = g_getShopItemPriceOriginal(self, price, item, customer, keeper, selling, shop);
            if (const std::optional<std::int32_t> ours = VendorModule::Get().PriceFor(shop, item, selling != 0); ours.has_value() && result != nullptr) {
                *result = *ours;
            }
            return result;
        }

        /**
         * The single point a completed trade moves money and items.
         *
         * For the session's shop the deal becomes a basket for the server and
         * the original does not run. `Accept`, which called this, then runs
         * `Close` as it always does, and that is what hands every basket item
         * back to where it came from -- so the screen is left exactly as it was
         * before the player pressed Trade, until the server's answer arrives.
         */
        void __fastcall ApplyTransaction_Hook(SDK::wh::shopmodule::C_ShoppingTransaction *transaction, std::int32_t price, const SDK::wh::framework::WUID *keeper) {
            if (VendorModule::Get().InterceptCommit(transaction)) {
                return;
            }
            g_applyTransactionOriginal(transaction, price, keeper);
        }

        /**
         * The filter the trade screen keeps on the player's column. The game's
         * own reasons -- quest items, untradable classes -- stand; past them,
         * the session's buy list decides. The shop's DB row has no filters, so
         * without this every ordinary item would be offered.
         */
        std::int32_t __fastcall GetShopSaleRefusal_Hook(const void *self, const SDK::wh::entitymodule::C_Item *item, const void *row) {
            const std::int32_t refusal = g_getShopSaleRefusalOriginal(self, item, row);
            if (refusal != 0) {
                return refusal;
            }
            return VendorModule::Get().RefusesSale(item) ? kRefusedByShop : 0;
        }

        /**
         * Money paid to or by the keeper with no deal at all: what
         * `Shop.AcceptTransaction` does when it finds no open transaction. The
         * player's purse is real, so during a session this would be a payment
         * the server never agreed to; it is refused outright.
         */
        void __fastcall TransferMoneyWithKeeper_Hook(void *unused, SDK::wh::rpgmodule::I_Soul *keeper, std::int32_t amount) {
            if (VendorModule::Get().BlocksDirectPayment()) {
                return;
            }
            g_transferMoneyWithKeeperOriginal(unused, keeper, amount);
        }
    } // namespace

    void VendorModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__GetShopItemPrice), reinterpret_cast<LPVOID>(&GetShopItemPrice_Hook), reinterpret_cast<LPVOID *>(&g_getShopItemPriceOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ShoppingTransaction__ApplyTransaction), reinterpret_cast<LPVOID>(&ApplyTransaction_Hook), reinterpret_cast<LPVOID *>(&g_applyTransactionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.shopmodule__GetShopSaleRefusal), reinterpret_cast<LPVOID>(&GetShopSaleRefusal_Hook), reinterpret_cast<LPVOID *>(&g_getShopSaleRefusalOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.shopmodule__TransferMoneyWithKeeper), reinterpret_cast<LPVOID>(&TransferMoneyWithKeeper_Hook), reinterpret_cast<LPVOID *>(&g_transferMoneyWithKeeperOriginal));
    }
} // namespace KCDC::Features::Vendor

static InitFunction init(
    []() {
        KCDC::Features::Vendor::VendorModule::Install();
    },
    "Vendor");
