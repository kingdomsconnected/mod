#include "vendor_shop.h"
#include "features/items/custom_item_module.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"

#include "common/module_support.h"

#include "shared/game/item_classes.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/shopmodule/c_shop_module.h"

#include <logging/logger.h>

#include <cstdio>

namespace KCDC::Features::Vendor {
    namespace {
        constexpr const char *kLogger = "Vendor";

        /**
         * `test_keys` in `Libs/Tables/shop/shop__test.xml`: no item filters and
         * `test_empty_preset`, so the shop comes up with nothing on the shelf.
         */
        constexpr float kEmptyShopDbId = 789.0F;

        /**
         * The high bits every shop this mod spawns carries in its EntityGuid.
         *
         * The shop derives its WUID from the GUID, and the shop module keys
         * both its registry and the open transaction by that WUID, so two
         * shops on one GUID would share a deal. Distinct from the stash tag.
         */
        constexpr std::uint64_t kShopGuidTag  = 0xF5A6000000000000ULL;
        constexpr std::uint64_t kShopGuidMask = 0x0000FFFFFFFFFFFFULL;
    } // namespace

    bool VendorShop::Build(SDK::wh::framework::WUID keeperSoulWuid, const SDK::Vec3 &position) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *registry     = entitySystem != nullptr ? entitySystem->GetClassRegistry() : nullptr;
        if (registry == nullptr) {
            return false;
        }
        SDK::IEntityClass *entityClass = registry->FindClass("Shop");
        if (entityClass == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("The 'Shop' entity class is not registered; no vendor can open");
            return false;
        }

        const std::uint32_t ordinal = _nextOrdinal++;
        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Shop_%u", ordinal);

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.guid        = kShopGuidTag | (static_cast<std::uint64_t>(ordinal) & kShopGuidMask);
        params.name        = name;
        params.position    = position;

        // Uninitialised, so the DB row is in the properties before the
        // shop proxy's init reads `Properties.iShopId` and builds the stock.
        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("SpawnEntity refused the shop");
            return false;
        }
        const SDK::EntityId entityId = entity->GetId();

        auto *script                   = entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script));
        SDK::IScriptTable *scriptTable = script != nullptr ? script->GetScriptTable() : nullptr;
        auto properties                = scriptTable != nullptr ? scriptTable->GetTable("Properties") : SDK::ScriptTablePtr {};
        if (!properties) {
            entitySystem->RemoveEntity(entityId, false);
            Framework::Logging::GetLogger(kLogger)->warn("The shop entity came up without a Properties table");
            return false;
        }
        properties->SetValue("iShopId", kEmptyShopDbId);

        // InitEntity queues the entity for deletion on failure.
        if (!entitySystem->InitEntity(entity, params) || KCDC::Common::ResolveEntity(entityId) == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("InitEntity refused the shop");
            return false;
        }

        // The shop module swaps the Shop class's user-proxy creator for the
        // shop itself, a light game object filed under its own proxy id: there
        // is no CGameObject here to ask for an extension.
        auto *shop = SDK::wh::shopmodule::C_Shop::FromProxy(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::GameObjectLight)));
        if (shop == nullptr || shop->GetStock() == nullptr) {
            entitySystem->RemoveEntity(entityId, false);
            Framework::Logging::GetLogger(kLogger)->warn("The shop entity came up without a C_Shop or its stock");
            return false;
        }

        shop->AddKeeper(&keeperSoulWuid);
        _shop     = shop;
        _entityId = entityId;
        _keeper   = keeperSoulWuid;
        Framework::Logging::GetLogger(kLogger)->debug("Built shop entity {} (row {}, keeper {:#x})", entityId, shop->GetShopDbId(), keeperSoulWuid.value);
        return true;
    }

    void VendorShop::Destroy() {
        _stockProjection.Clear();
        if (_transaction != nullptr) {
            _transaction->Close();
        }
        if (_entityId != 0) {
            if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
                entitySystem->RemoveEntity(_entityId, false);
            }
        }
        _shop        = nullptr;
        _transaction = nullptr;
        _entityId    = 0;
        _keeper      = {};
    }

    void VendorShop::Forget() {
        _stockProjection.Clear();
        _shop        = nullptr;
        _transaction = nullptr;
        _entityId    = 0;
        _keeper      = {};
    }

    bool VendorShop::Restock(const std::vector<Shared::RPC::VendorStockRow> &stock, std::uint32_t purse) {
        if (!KCDC::Features::Items::CustomItemModule::Get().Ready()) {
            return false;
        }
        SDK::wh::entitymodule::C_Inventory *inventory = _shop != nullptr ? _shop->GetStock() : nullptr;
        if (inventory == nullptr) {
            return false;
        }

        if (_transaction != nullptr && _transaction->GetBasketCount() != 0) {
            return false;
        }

        std::map<std::string, nlohmann::json> rows;
        for (const auto &row : stock) {
            const auto guid = Shared::Game::CryGuidToString(row.itemClass.low, row.itemClass.high);
            rows[guid]      = {{"item", guid}, {"amount", row.amount}, {"metadata", Shared::Inventory::EncodeItemProperties(row.properties)}};
        }
        if (purse != 0) {
            nlohmann::json metadata;
            std::string error;
            if (!Shared::Inventory::NormalizeAuthoritativeItemProperties(std::string(Shared::Game::kMoneyItemClassGuid), nlohmann::json::object(), metadata, error)) {
                return false;
            }
            rows["purse"] = {{"item", Shared::Game::kMoneyItemClassGuid}, {"amount", purse}, {"metadata", std::move(metadata)}};
        }
        std::map<std::string, std::string> unshown;
        std::string error;
        return _stockProjection.Apply(inventory, rows, unshown, error) && unshown.empty();
    }

    void VendorShop::Open() {
        if (_shop == nullptr) {
            return;
        }
        // The server's price list is the only one; there is nothing to haggle.
        _shop->SetHaggleAllowed(false);
        _shop->OpenForKeeper(_keeper);

        // Already created by the open; this finds it, and binding it again
        // changes nothing. The deal lives in the module, not on the shop.
        Rebind();
    }

    void VendorShop::Rebind() {
        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *shops       = environment != nullptr ? environment->GetShopModule() : nullptr;
        if (_shop == nullptr || shops == nullptr || (_transaction != nullptr && _transaction->GetShop() == _shop)) {
            return;
        }
        _transaction = shops->GetOrCreateTransaction(_shop->GetWuid().value);
    }
} // namespace KCDC::Features::Vendor
