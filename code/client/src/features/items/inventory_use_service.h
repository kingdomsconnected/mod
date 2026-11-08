#pragma once

#include "core/feature_registry.h"
#include "features/items/inventory_use_handlers.h"

#include <v8.h>

namespace SDK::wh::entitymodule {
    class C_Item;
}
namespace Framework::Scripting {
    class ResourceManager;
}

namespace KCDC::Features::Items {
    /** Resource-owned overrides of local inventory actions. All access is on the game thread. */
    class InventoryUseService final: public KCDC::Core::IFeatureService {
      public:
        using Handlers = KCDC::Features::Items::InventoryUseHandlers<v8::Global<v8::Function>>;
        static InventoryUseService &Get();
        void AttachResources(Framework::Scripting::ResourceManager *resources);
        std::uint32_t Add(const std::string &resource, Handlers::Selector selector, v8::Isolate *isolate, v8::Local<v8::Function> callback);
        bool Remove(std::uint32_t id, const std::string &resource);
        bool HasHandler(const SDK::wh::entitymodule::C_Item *item) const;
        /** True means suppress the native action, including when the inventory cannot yet be spent. */
        bool Capture(SDK::wh::entitymodule::C_Item *item, const char *action);
        void Update() override;
        void Reset() override;

      private:
        Handlers _handlers;
        Framework::Scripting::ResourceManager *_resources = nullptr;
    };
} // namespace KCDC::Features::Items
