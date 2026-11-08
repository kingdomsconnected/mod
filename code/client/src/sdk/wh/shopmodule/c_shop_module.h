#pragma once

#include "../../patterns.h"

#include "c_shopping_transaction.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK::wh::shopmodule {
    /**
     * The shops of the loaded world and their deals, at
     * `C_GlobalEnvironment+0x110` (vtable 0x183F81328).
     *
     * It keeps every `C_Shop` by its WUID behind the pointer at +0x10 (filled
     * by the shop's own init) and every transaction by the shop WUID it was
     * opened for in the map at +0x28. Nothing here reads either directly, so no member is mapped
     * and the size is not asserted.
     */
    class C_ShopModule final {
      public:
        /**
         * The deal for a shop, created on first use and bound to the local
         * player and that shop every time (0x180C47C10). `C_Shop::OpenForKeeper`
         * is its caller on the game's side. A key other than the repair key
         * always yields a `C_ShoppingTransaction`.
         */
        C_ShoppingTransaction *GetOrCreateTransaction(std::uint64_t shopWuid) {
            return hook::this_call<C_ShoppingTransaction *>(gPatterns.C_ShopModule__GetOrCreateTransaction, this, shopWuid);
        }
    };
} // namespace SDK::wh::shopmodule
