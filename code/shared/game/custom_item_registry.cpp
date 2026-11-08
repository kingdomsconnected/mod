#include "custom_item_registry.h"

#include "shared/image/png_image.h"

#include <utils/crypto.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace KCDC::Shared::Game {
    namespace {
        // The next integer rounds to 2^31 as a float, overflowing the native
        // signed conversion in GetCurrentUnitPrice/GetNewUnitPrice.
        constexpr std::uint32_t kMaximumCustomItemPrice = 2147483583U;

        std::string Guid(const std::string &value) {
            const auto hash = Framework::Utils::Crypto::Sha256Hex(value);
            if (hash.size() != 64) {
                throw std::runtime_error("Unable to generate item identity");
            }
            return hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-" + hash.substr(12, 4) + "-" + hash.substr(16, 4) + "-" + hash.substr(20, 12);
        }

        bool Text(const nlohmann::json &value, std::size_t maximum) {
            if (!value.is_string() || value.get_ref<const std::string &>().size() > maximum) {
                return false;
            }
            const auto &text = value.get_ref<const std::string &>();
            return std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; });
        }

        // A model or material path as the item tables write it, relative to
        // Objects/: `manmade/common_tools/hammer.cgf`, or for one a resource
        // streams, `kcdc/<resource>/<file>`. The game prefixes "Objects/"
        // itself (S_PickableItemClass::GetModelAndMaterial, vslot 0x1B8 at
        // 0x180978B44; the model at 0x180978CE4), so a path that already
        // starts with it would name Objects/objects/... and never load.
        bool ObjectPath(const nlohmann::json &value) {
            if (!Text(value, 256) || value.get_ref<const std::string &>().empty()) {
                return false;
            }
            const auto &path  = value.get_ref<const std::string &>();
            const bool rooted = path.size() >= 8 && std::equal(path.begin(), path.begin() + 8, "objects/", [](unsigned char a, unsigned char b) {
                return std::tolower(a) == b;
            });
            return !rooted && path.front() != '/' && path.find("..") == std::string::npos && std::all_of(path.begin(), path.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
            });
        }

        bool BoundedDepth(const nlohmann::json &value, std::size_t depth = 0) {
            if (!value.is_structured()) {
                return true;
            }
            // Leave room for the inventory/event envelopes when a script
            // receives a deeply frozen row (whose traversal limit is 64).
            if (depth >= 48) {
                return false;
            }
            return std::all_of(value.begin(), value.end(), [depth](const nlohmann::json &child) { return BoundedDepth(child, depth + 1); });
        }
    } // namespace

    nlohmann::json CustomItemDefinition::Encode() const {
        nlohmann::json encoded = {{"name", name}, {"description", description}, {"weight", weight}, {"price", price}, {"stackable", stackable}, {"usable", usable}, {"useLabel", useLabel}};
        // Optional fields are absent rather than empty, so a definition that
        // borrows its whole look from a visual keeps its native GUID.
        const std::pair<const char *, const std::string *> optional[] = {{"visual", &visual}, {"icon", &icon}, {"model", &model}, {"material", &material}};
        for (const auto &[key, value] : optional) {
            if (!value->empty()) {
                encoded[key] = *value;
            }
        }
        return encoded;
    }

    CustomItemRegistry &CustomItemRegistry::Get() {
        static CustomItemRegistry registry;
        return registry;
    }

    const CustomItemDefinition &CustomItemRegistry::Register(const std::string &id, const nlohmann::json &definition) {
        std::unique_lock lock(_mutex);
        if (id.empty() || id.size() > 128 || !std::all_of(id.begin(), id.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':' || c == '_' || c == '-' || c == '.'; })) {
            throw std::invalid_argument("Invalid custom item id");
        }
        if (!definition.is_object() || !definition.contains("name") || !Text(definition.at("name"), 256) || definition.at("name").get_ref<const std::string &>().empty()) {
            throw std::invalid_argument("Custom items require a name");
        }
        if (definition.contains("visual") ? !definition.at("visual").is_string() : !definition.contains("model") || !definition.contains("icon")) {
            throw std::invalid_argument("Custom items require a visual, or their own model and icon");
        }
        for (const auto &[key, value] : definition.items()) {
            if (key != "name" && key != "description" && key != "visual" && key != "weight" && key != "price" && key != "stackable" && key != "usable" && key != "useLabel" && key != "icon" && key != "model" && key != "material") {
                throw std::invalid_argument("Unknown custom item property: " + key);
            }
        }
        for (const auto *key : {"description", "useLabel"}) {
            if (definition.contains(key) && !Text(definition.at(key), std::string_view(key) == "description" ? 4096 : 64)) {
                throw std::invalid_argument("Invalid custom item text");
            }
        }
        for (const auto *key : {"stackable", "usable"}) {
            if (definition.contains(key) && !definition.at(key).is_boolean()) {
                throw std::invalid_argument("Invalid custom item flag");
            }
        }
        if (!definition.contains("weight") || !definition.at("weight").is_number()) {
            throw std::invalid_argument("Custom items require a weight");
        }
        const double weight = definition.at("weight").get<double>();
        if (!std::isfinite(weight) || weight < 0 || weight > 1000000) {
            throw std::invalid_argument("Invalid custom item weight");
        }
        if (definition.contains("price") && (!definition.at("price").is_number_integer() || definition.at("price").get<double>() < 0 || definition.at("price").get<double>() > kMaximumCustomItemPrice)) {
            throw std::invalid_argument("Invalid custom item price");
        }
        CustomItemDefinition parsed;
        if (definition.contains("visual")) {
            const auto visual = definition.at("visual").get<std::string>();
            const auto found  = std::find_if(g_ItemClasses.begin(), g_ItemClasses.end(), [&](const ItemClassEntry &entry) {
                return entry.guid == visual || entry.name == visual;
            });
            if (found == g_ItemClasses.end() || (found->category != "MiscItem" && found->category != "Misc" && found->category != "CraftingMaterial" && found->category != "Document")) {
                throw std::invalid_argument("Unsupported custom item visual: use a miscellaneous item, crafting material or document");
            }
            parsed.visual = found->guid;
        }
        for (const auto *key : {"model", "material"}) {
            if (definition.contains(key) && !ObjectPath(definition.at(key))) {
                throw std::invalid_argument("Custom item model and material are paths under Objects/, as the game's item tables write them");
            }
        }
        if (definition.contains("material") && !definition.contains("model")) {
            throw std::invalid_argument("A custom item material needs its own model");
        }
        parsed.model       = definition.value("model", std::string());
        parsed.material    = definition.value("material", std::string());
        parsed.id          = id;
        parsed.guid        = Guid("KCDC/custom-item/id/" + id);
        parsed.name        = definition.at("name").get<std::string>();
        parsed.description = definition.value("description", std::string());
        parsed.weight      = static_cast<float>(weight);
        parsed.price       = definition.value("price", 0U);
        parsed.stackable   = definition.value("stackable", true);
        parsed.usable      = definition.value("usable", false);
        parsed.useLabel    = definition.value("useLabel", std::string("Use"));
        if (parsed.useLabel.empty()) {
            throw std::invalid_argument("Custom item useLabel cannot be empty");
        }
        if (definition.contains("icon")) {
            const auto &icon = definition.at("icon");
            if (!icon.is_string() || !KCDC::Shared::Image::ReadBase64Png(icon.get_ref<const std::string &>(), parsed.iconPng)) {
                throw std::invalid_argument("Custom item icon must be a base64 PNG of at most 256 KiB and 4096x4096 pixels");
            }
            parsed.icon   = icon.get<std::string>();
            parsed.iconId = "kcdc_ic_" + Framework::Utils::Crypto::Sha256Hex(parsed.iconPng).substr(0, 16);
        }
        parsed.nativeGuid = Guid("KCDC/custom-item/native/" + id + "/" + parsed.Encode().dump());
        if (const auto match = _definitions.find(parsed.guid); match != _definitions.end()) {
            if (match->second.id != id || match->second.Encode() != parsed.Encode()) {
                throw std::invalid_argument("Custom item id is already registered with another definition");
            }
            return match->second;
        }
        if (_definitions.size() >= 4096) {
            throw std::invalid_argument("Custom item registry capacity exceeded");
        }
        const auto guid     = parsed.guid;
        auto &stored        = _definitions.emplace(guid, std::move(parsed)).first->second;
        stored.item         = {stored.id, stored.guid, "MiscItem", stored.price, {}};
        stored.properties   = {stored.guid, 1, false, false, false, false, stored.stackable};
        stored.registeredAt = ++_revision;
        return stored;
    }

    const CustomItemDefinition *CustomItemRegistry::Find(std::string_view guid) const {
        std::shared_lock lock(_mutex);
        const auto found = _definitions.find(guid);
        return found == _definitions.end() ? nullptr : &found->second;
    }

    const CustomItemDefinition *CustomItemRegistry::FindNative(std::string_view guid) const {
        std::shared_lock lock(_mutex);
        for (const auto &[id, definition] : _definitions) {
            if (definition.nativeGuid == guid) {
                return &definition;
            }
        }
        return nullptr;
    }

    std::string CustomItemRegistry::Resolve(std::string_view id) const {
        std::shared_lock lock(_mutex);
        if (const auto definition = _definitions.find(id); definition != _definitions.end()) {
            return definition->second.guid;
        }
        for (const auto &[guid, definition] : _definitions) {
            if (definition.id == id) {
                return guid;
            }
        }
        return {};
    }

    void CustomItemRegistry::Reset() {
        std::unique_lock lock(_mutex);
        _definitions.clear();
        _stackSecret.clear();
        _revision = 0;
    }

    bool CustomItemRegistry::NormalizeCustom(const nlohmann::json &input, nlohmann::json &output) {
        if (!input.is_object() || !BoundedDepth(input) || input.dump().size() > 16384) {
            return false;
        }
        output = nlohmann::json::object();
        for (const auto &[key, value] : input.items()) {
            if (key == "name" || key == "description") {
                if (value.is_null()) {
                    continue;
                }
                if (!Text(value, key == "name" ? 256 : 4096)) {
                    return false;
                }
            }
            else if (key == "data") {
                if (value.is_null()) {
                    continue;
                }
                if (!value.is_object()) {
                    return false;
                }
            }
            else {
                return false;
            }
            output[key] = value;
        }
        return true;
    }

    nlohmann::json CustomItemRegistry::PublicMetadata(const nlohmann::json &metadata) {
        auto result = metadata;
        if (result.contains("custom")) {
            if (_stackSecret.empty()) {
                bool ok           = false;
                const auto secret = Framework::Utils::Crypto::GenerateKey(&ok);
                if (!ok) {
                    throw std::runtime_error("Unable to generate inventory equality secret");
                }
                _stackSecret = Framework::Utils::Crypto::ToHex(secret.data(), secret.size());
            }
            result["customStackKey"] = Framework::Utils::Crypto::Sha256Hex(_stackSecret + metadata.at("custom").dump());
            result["custom"].erase("data");
        }
        return result;
    }
} // namespace KCDC::Shared::Game
