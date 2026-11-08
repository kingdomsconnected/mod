#pragma once

#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/custom_item_registry.h"
#include "shared/item_stack.h"
#include <algorithm>
#include <string>

namespace KCDC::Features::Items {
    /** Labels and class IDs at the native presentation boundary. */
    class CustomItemPresentation final {
      public:
        static std::string TypeKey(const std::string &guid, const char *field) {
            std::string key = "kcdc_it_" + guid + "_" + field;
            std::erase(key, '-');
            return key;
        }

        static std::string TypeLabel(const std::string &guid, const char *field) {
            return "@" + TypeKey(guid, field);
        }

        static std::string InstanceLabel(std::uint64_t id, bool description = false) {
            return "@kcdc_ii_" + std::to_string(id) + (description ? "_d" : "_n");
        }

        static SDK::wh::entitymodule::S_ItemClassGUID ToNative(const KCDC::Shared::RPC::ItemClassId &id) {
            const auto *definition = KCDC::Shared::Game::CustomItemRegistry::Get().Find(KCDC::Shared::Game::CryGuidToString(id.low, id.high));
            const auto native      = definition != nullptr ? KCDC::Shared::Game::ItemClassIdFromGuid(definition->nativeGuid) : id;
            return {native.low, native.high};
        }
    };
} // namespace KCDC::Features::Items
