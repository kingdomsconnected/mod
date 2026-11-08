#include "inventory_use_scripting.h"

#include "core/scripting/script_context.h"
#include "inventory_use_service.h"
#include "shared/game/custom_item_registry.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

namespace KCDC::Scripting {
    namespace {
        using Service = KCDC::Features::Items::InventoryUseService;

        void Throw(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, message)));
        }

        std::string ResolveItem(const std::string &text) {
            const auto custom = KCDC::Shared::Game::CustomItemRegistry::Get().Resolve(text);
            if (!custom.empty()) {
                return custom;
            }
            const auto parsed = KCDC::Shared::Game::ItemClassIdFromGuid(text);
            if (parsed != KCDC::Shared::RPC::ItemClassId {}) {
                if (const auto *item = KCDC::Shared::Game::FindItemClass(parsed)) {
                    return std::string(item->guid);
                }
            }
            for (const auto &item : KCDC::Shared::Game::g_ItemClasses) {
                if (item.name == text) {
                    return std::string(item.guid);
                }
            }
            return {};
        }

        void OnUse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 2 || !info[1]->IsFunction()) {
                Throw(isolate, "Inventory.onUse expects an item name/GUID or { uid: string }, and a handler");
                return;
            }
            Service::Handlers::Selector selector;
            if (info[0]->IsString()) {
                selector.item = ResolveItem(v8pp::from_v8<std::string>(isolate, info[0]));
                if (selector.item.empty()) {
                    Throw(isolate, "Inventory.onUse: unknown item name or class GUID");
                    return;
                }
            }
            else {
                v8::Local<v8::Value> uid;
                if (!info[0]->IsObject() || !info[0].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "uid")).ToLocal(&uid)) {
                    return Throw(isolate, "Inventory.onUse expects an item name/GUID or { uid: string }");
                }
                if (!uid->IsString() || !Service::Handlers::ParseUid(v8pp::from_v8<std::string>(isolate, uid), selector.uid)) {
                    return Throw(isolate, "Inventory.onUse: uid must be a positive uint64 decimal string without leading zeroes");
                }
            }
            const auto callback = info[1].As<v8::Function>();
            const auto resource = KCDC::ResolveCallingResource(isolate, callback);
            const auto id       = Service::Get().Add(resource, std::move(selector), isolate, callback);
            if (id == 0) {
                return Throw(isolate, "Inventory.onUse requires a running client resource and supports at most 256 handlers");
            }
            info.GetReturnValue().Set(id);
        }

        void OffUse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const bool removed = info.Length() == 1 && info[0]->IsUint32() && Service::Get().Remove(info[0].As<v8::Uint32>()->Value(), KCDC::ResolveCallingResource(info.GetIsolate()));
            info.GetReturnValue().Set(removed);
        }
    } // namespace

    void RegisterInventoryUse(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        Service::Get().Reset();
        Service::Get().AttachResources(KCDC::ScriptResources());
        auto &catalog = ClientCatalog();
        auto &use     = catalog.data_type("InventoryUse", "A local inventory action intercepted before the game applies it. A snapshot, not permission to spend an item on the server.");
        use.add_property("item", "string", "Item class GUID, using the logical GUID for custom items.");
        use.add_property("uid", "string", "Native instance UID as a lossless decimal string. Local to this client; splits, merges and reprojection may replace it.");
        use.add_property("id", "string", "A server inventory row represented by the selected native item. Send this to a server resource to request consumption.");
        use.add_property("revision", "number", "The inventory revision at the time of the action. The server must validate it and current ownership.");
        use.add_property("amount", "number", "Always 1. The override requests one use, even when a native row represents a stack.");
        use.add_property("action", "\"primary\" | \"secondary\" | \"activate\"", "Which native path was intercepted. Eat/Drink and Learn use secondary; primary also includes equipment actions; activate is the native equip-toggle path.");
        v8pp::module inventory(isolate, catalog, "Inventory",
            "Client-side overrides for actions in the local player's native inventory. Register by catalog item name, class GUID, custom item id, or { uid } for one native instance. "
            "A matching handler replaces the action before native consumption or effects. Scripts run on the next feature update, outside the native detour; return values are ignored and errors do not restore the native action. "
            "Only owned, synchronized inventory items dispatch. Pending uses are discarded if their handler, native item, server row or inventory revision disappears or changes. "
            "Instance handlers take priority over class handlers; otherwise the newest matching registration wins. Removing it reveals the earlier handler. Resource stop and session end remove handlers. "
            "The server remains responsible for validating requests, consuming inventory and applying effects. This API does not intercept quick-slot actions outside the inventory.");
        inventory.function("onUse", &OnUse,
            v8pp::metadata::docs("number",
                {v8pp::metadata::param("item", "string | { uid: string }", false, "Catalog name such as apple, a class GUID, a custom item id, or a native UID from InventoryUse.uid."),
                    v8pp::metadata::param("handler", "(use: InventoryUse) => void", false, "Runs instead of the native action. No item is automatically consumed.")},
                "Registers an inventory action override for this resource. Adds a Use action even for an item without a normal primary action. Duplicate presses on the same UID within one frame are combined.",
                "A registration id for offUse. Throws for an unknown class, invalid UID, missing resource or more than 256 active registrations."));
        inventory.function("offUse", &OffUse,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("handlerId", "number", false, "Id returned by onUse.")}, "Removes a handler owned by the calling resource and discards its pending uses.", "False if the registration is missing or belongs to another resource."));
        inventory.publish(global);
    }
} // namespace KCDC::Scripting
