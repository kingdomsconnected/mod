#pragma once

#include "../../cryengine/entitysystem/i_entity_proxy.h"
#include "../entitymodule/c_inventory.h"
#include "../entitymodule/i_game_object_light.h"
#include "../entitymodule/i_item_collection_listener.h"
#include "../framework/i_schedule_updatable.h"
#include "../framework/wuid.h"
#include "c_shop_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shopmodule {
    /**
     * The shop behind every `Shop` entity: a stock inventory, the souls that
     * keep it, and the row of `Libs/Tables/shop/shop.xml` it prices by.
     *
     * Not a game object extension: a `Shop` entity has no `CGameObject` at
     * all. `C_ShopModule::Init` (0x1819A1A78) finds the `Shop` entity class
     * that `Entities/Shop.ent` registers and overwrites its user-proxy creator
     * with `C_Shop::CreateUserProxy` (0x180D11A10), so the proxy the class
     * creates *is* the shop, as its `I_GameObjectLight` base. That base's
     * `GetType` answers `EEntityProxy::GameObjectLight` (18), not `User`, and
     * the entity files it under that id.
     *
     * The base's `Init` (slot 8, 0x1804579E0) runs `InitFromEntityProperties`
     * (slot 17, 0x18096DBDC), which reads `Properties.iShopId` and calls
     * `LoadShopData`, which always creates the stock inventory -- so a shop
     * whose entity initialised always has one.
     *
     * Allocated at 0x1B8 by `C_Shop::Create` (0x180D11A6C). RTTI and the
     * constructor at 0x180D11AF0 establish the bases at +0x00, +0x80, +0xB0
     * (registered on the stock inventory) and +0xB8. RTTI also reports an
     * empty `UnsafeOp::CryDeferrable<0>` tag at +0xC0; it overlaps the first
     * data member and is omitted, as on `C_ItemSlot`.
     *
     * The primary table at 0x183A949B8 extends `C_ShopBase`'s 13 slots by
     * three whose names are not proven.
     */
    class C_Shop
        : public C_ShopBase
        , public entitymodule::I_GameObjectLight
        , public entitymodule::I_ItemCollectionListener
        , public framework::I_ScheduleUpdatable {
      public:
        ~C_Shop() override = default;

        virtual void unk_shop_fn0013() = 0;
        virtual void unk_shop_fn0014() = 0;
        virtual void unk_shop_fn0015() = 0;

        /** The shop behind a `Shop` entity's `EEntityProxy::GameObjectLight` proxy. */
        static C_Shop *FromProxy(IEntityProxy *proxy) {
            return static_cast<C_Shop *>(proxy);
        }

        /**
         * The shop's own WUID, keyed in the shop module's registry and the one
         * its transaction is created under. Derived from the entity GUID with
         * type byte 4, so two shops need two GUIDs.
         */
        framework::WUID GetWuid() const {
            return m_wuid;
        }

        /**
         * What the shop sells. Built by `LoadShopData` (0x18096E614) through
         * `C_InventoryManager::GetOrCreateInventory` and filled from the DB
         * row's preset; destroyed with the shop.
         *
         * An item is the shop's -- a buy on the trade screen -- exactly when
         * its owner is this inventory: that is the whole of the test the
         * transaction's commit makes (0x1805663FC).
         */
        entitymodule::C_Inventory *GetStock() const {
            return m_pStock;
        }

        /** The `shop_id` of the DB row, read from `Properties.iShopId` at init. */
        std::uint32_t GetShopDbId() const {
            return m_shopDbId;
        }

      private:
        std::byte m_inventoryGuid00C0[0x10]; // 0x0C0, CryGUID the stock inventory is created under
        framework::WUID m_wuid;              // 0x0D0
        std::byte m_shopName00D8[0x8];       // 0x0D8, CryStringT sShopName
        entitymodule::C_Inventory *m_pStock; // 0x0E0
        std::uint64_t m_state00E8[4];  // 0x0E8, the ctor (0x180D11AF0) zeroes
        std::int32_t m_state0108;      // 0x108, ctor sets -1
        std::uint32_t m_state010C;     // 0x10C, ctor sets 0x70007
        std::uint64_t m_state0110[4];  // 0x110, ctor zeroes
        std::byte m_unset0130[0x18];   // 0x130, not written by the ctor
        std::uint8_t m_flag0148;       // 0x148, ctor sets 0
        std::byte m_padding0149[0x7];  // 0x149
        std::uint64_t m_state0150[6];  // 0x150, ctor zeroes
        framework::WUID m_wuid0180;    // 0x180, ctor sets qword_185333208
        framework::WUID m_wuid0188;    // 0x188, ctor sets the invalid WUID qword_185333210
        std::byte m_keepers0190[0x10]; // 0x190, std::list of keeper WUIDs, walked by IsKeeper
        std::uint64_t m_state01A0;     // 0x1A0, ctor zeroes
        framework::WUID m_keeperWuid; // 0x1A8, the one WUID IsKeeper accepts past the list
        std::uint32_t m_shopDbId;     // 0x1B0
        std::byte m_padding01B4[0x4]; // 0x1B4

        static void AssertLayout() {
            static_assert(offsetof(C_Shop, m_inventoryGuid00C0) == 0xC0, "C_Shop::m_inventoryGuid00C0 offset mismatch");
            static_assert(offsetof(C_Shop, m_wuid) == 0xD0, "C_Shop::m_wuid offset mismatch");
            static_assert(offsetof(C_Shop, m_pStock) == 0xE0, "C_Shop::m_pStock offset mismatch");
            static_assert(offsetof(C_Shop, m_state0108) == 0x108, "C_Shop::m_state0108 offset mismatch");
            static_assert(offsetof(C_Shop, m_flag0148) == 0x148, "C_Shop::m_flag0148 offset mismatch");
            static_assert(offsetof(C_Shop, m_wuid0180) == 0x180, "C_Shop::m_wuid0180 offset mismatch");
            static_assert(offsetof(C_Shop, m_state01A0) == 0x1A0, "C_Shop::m_state01A0 offset mismatch");
            static_assert(offsetof(C_Shop, m_keepers0190) == 0x190, "C_Shop::m_keepers0190 offset mismatch");
            static_assert(offsetof(C_Shop, m_keeperWuid) == 0x1A8, "C_Shop::m_keeperWuid offset mismatch");
            static_assert(offsetof(C_Shop, m_shopDbId) == 0x1B0, "C_Shop::m_shopDbId offset mismatch");
        }
    };

    static_assert(sizeof(C_Shop) == 0x1B8, "C_Shop size mismatch");
} // namespace SDK::wh::shopmodule
