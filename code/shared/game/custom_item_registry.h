#pragma once

#include "inventory_property_classes.h"
#include "item_classes.h"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <vector>

namespace KCDC::Shared::Game {
    // Every stock item icon (Libs/UI/Textures/Icons/Items/*_icon.dds, all 3574 in
    // IPL_GameData.pak) is 64x64 BC7 with the item reaching the edges, and the UI draws
    // the image at its own size: a 128 px PNG showed twice as large as its row.
    constexpr std::uint32_t kCustomItemIconSide = 64;

    struct CustomItemDefinition {
        std::uint64_t registeredAt = 0;
        std::string id;
        std::string guid;
        std::string nativeGuid;
        std::string name;
        std::string description;
        std::string visual;
        std::string useLabel;
        // Optional inventory icon: base64 PNG as the script gave it, its bytes,
        // and the IconId the native class carries (content-addressed).
        std::string icon;
        std::string iconPng;
        std::string iconId;
        // Optional own model and material, relative to Objects/. Without a
        // model both come from the visual; an own model without a material
        // draws with the one the model names itself.
        std::string model;
        std::string material;
        float weight        = 0.0F;
        std::uint32_t price = 0;
        bool stackable      = true;
        bool usable         = false;
        ItemClassEntry item;
        InventoryPropertyClass properties;

        nlohmann::json Encode() const;
    };

    /** Session definitions; the client separately owns native object lifetime. */
    class CustomItemRegistry final {
      public:
        static CustomItemRegistry &Get();
        const CustomItemDefinition &Register(const std::string &id, const nlohmann::json &definition);
        const CustomItemDefinition *Find(std::string_view guid) const;
        const CustomItemDefinition *FindNative(std::string_view guid) const;
        std::string Resolve(std::string_view id) const;
        const std::map<std::string, CustomItemDefinition, std::less<>> &Definitions() const {
            return _definitions;
        }
        std::uint64_t Revision() const {
            return _revision;
        }
        void Reset();
        /** Removes script data and supplies an opaque equality token. */
        nlohmann::json PublicMetadata(const nlohmann::json &metadata);
        static bool NormalizeCustom(const nlohmann::json &input, nlohmann::json &output);

      private:
        mutable std::shared_mutex _mutex;
        std::map<std::string, CustomItemDefinition, std::less<>> _definitions;
        std::string _stackSecret;
        std::uint64_t _revision = 0;
    };
} // namespace KCDC::Shared::Game
