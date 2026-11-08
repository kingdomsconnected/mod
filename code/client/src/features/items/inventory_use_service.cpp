#include "inventory_use_service.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "custom_item_module.h"
#include "features/player/player_inventory_service.h"
#include "sdk/wh/entitymodule/c_entity_module.h"
#include "sdk/wh/entitymodule/c_item_manager.h"
#include "shared/game/item_classes.h"

#include <logging/logger.h>
#include <scripting/engine_helpers.h>
#include <v8pp/convert.hpp>

namespace KCDC::Features::Items {
    namespace {
        std::string ItemGuid(const SDK::wh::entitymodule::C_Item &item) {
            const auto logical = CustomItemModule::Get().ToLogical(item.GetItemClass()->GetGuid());
            const auto text    = KCDC::Shared::Game::CryGuidToText(logical.low, logical.high);
            return {text.data(), text.size()};
        }
    } // namespace

    InventoryUseService &InventoryUseService::Get() {
        static InventoryUseService instance;
        return instance;
    }

    void InventoryUseService::AttachResources(Framework::Scripting::ResourceManager *resources) {
        if (resources == nullptr || _resources == resources) {
            return;
        }
        _resources = resources;
        resources->AddOnResourceStopped([this](const std::string &resource) { _handlers.Release(resource); });
        resources->AddOnResourceError([this](const std::string &resource, const std::string &) { _handlers.Release(resource); });
    }

    std::uint32_t InventoryUseService::Add(const std::string &resource, Handlers::Selector selector, v8::Isolate *isolate, v8::Local<v8::Function> callback) {
        return _handlers.Add(resource, std::move(selector), v8::Global<v8::Function>(isolate, callback));
    }

    bool InventoryUseService::Remove(std::uint32_t id, const std::string &resource) {
        return _handlers.Remove(id, resource);
    }

    bool InventoryUseService::HasHandler(const SDK::wh::entitymodule::C_Item *item) const {
        return !_handlers.Empty() && item != nullptr && item->GetItemClass() != nullptr && _handlers.Match(ItemGuid(*item), item->GetId()) != 0;
    }

    bool InventoryUseService::Capture(SDK::wh::entitymodule::C_Item *item, const char *action) {
        if (_handlers.Empty() || item == nullptr || item->GetItemClass() == nullptr || KCDC::Core::gApplication == nullptr) {
            return false;
        }
        const auto guid    = ItemGuid(*item);
        const auto handler = _handlers.Match(guid, item->GetId());
        auto &inventory    = KCDC::Core::gApplication->GetPlayerInventoryService();
        // A class override must never take over a merchant's, horse's or another actor's items.
        if (handler == 0 || !inventory.ManagesNativeItem(item)) {
            return false;
        }
        if (inventory.CanSpend()) {
            auto units = inventory.UnitsOf(item->GetId());
            if (units.empty()) {
                units = inventory.UnwornUnitsOfKind(*item);
            }
            if (!units.empty()) {
                _handlers.Queue({handler, item->GetId(), inventory.SyncedRevision(), guid, units.front().id, action});
            }
        }
        return true;
    }

    void InventoryUseService::Update() {
        const auto pending = _handlers.Drain();
        if (pending.empty()) {
            return;
        }
        KCDC::WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            for (const auto &use : pending) {
                const auto *handler = _handlers.Find(use.handler);
                if (handler == nullptr || KCDC::Core::gApplication == nullptr) {
                    continue;
                }
                auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
                if (!inventory.CanSpend() || inventory.SyncedRevision() != use.revision) {
                    continue;
                }
                auto *entities = SDK::wh::entitymodule::C_EntityModule::GetInstance();
                auto *manager  = entities != nullptr ? entities->GetItemManager() : nullptr;
                auto *item     = manager != nullptr ? manager->FindItem(SDK::wh::framework::WUID {use.uid}) : nullptr;
                if (item == nullptr || !inventory.ManagesNativeItem(item) || ItemGuid(*item) != use.item) {
                    continue;
                }
                auto units = inventory.UnitsOf(use.uid);
                if (units.empty()) {
                    units = inventory.UnwornUnitsOfKind(*item);
                }
                if (std::none_of(units.begin(), units.end(), [&](const auto &unit) { return unit.id == use.id && unit.amount > 0; })) {
                    continue;
                }

                // Own local handles and strings before calling: scripts may remove themselves or stop a resource.
                const auto callback = handler->callback.Get(isolate);
                const auto resource = handler->resource;
                auto value          = v8::Object::New(isolate);
                value->Set(context, v8pp::to_v8(isolate, "item"), v8pp::to_v8(isolate, use.item)).Check();
                value->Set(context, v8pp::to_v8(isolate, "uid"), v8pp::to_v8(isolate, std::to_string(use.uid))).Check();
                value->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, use.id)).Check();
                value->Set(context, v8pp::to_v8(isolate, "revision"), v8pp::to_v8(isolate, static_cast<double>(use.revision))).Check();
                value->Set(context, v8pp::to_v8(isolate, "amount"), v8pp::to_v8(isolate, 1)).Check();
                value->Set(context, v8pp::to_v8(isolate, "action"), v8pp::to_v8(isolate, use.action)).Check();
                v8::Local<v8::Value> args[] = {value};
                const auto previousResource = resources->GetCurrentResourceContext();
                resources->SetCurrentResourceContext(resource);
                v8::TryCatch caught(isolate);
                const auto result = callback->Call(context, context->Global(), 1, args);
                resources->SetCurrentResourceContext(previousResource);
                if (result.IsEmpty() && caught.HasCaught()) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("[{}] Inventory.onUse handler error: {}", resource, Framework::Scripting::FormatV8Exception(isolate, caught, "unknown error"));
                }
            }
        });
    }

    void InventoryUseService::Reset() {
        _handlers.Reset();
    }
} // namespace KCDC::Features::Items
