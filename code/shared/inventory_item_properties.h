#pragma once

#include "game/cry_guid_text.h"
#include "game/custom_item_registry.h"
#include "item_stack.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>

namespace KCDC::Shared::Inventory {
    /**
     * An item's properties as scripts and the sync wire spell them.
     *
     * Only what the server can decide: quality, absolute health, normalized
     * condition, a missile's poison and charges, and the persistent on-equip
     * buffs. A default is left out, so equal items encode equal. Provenance
     * (owner, stolen-from) and equipment placement are the game's, not the
     * inventory's, and have no key.
     */
    inline nlohmann::json EncodeItemProperties(const KCDC::Shared::RPC::InventoryItem &item) {
        nlohmann::json properties = {{"quality", item.quality}, {"health", item.health}, {"condition", item.condition}};
        if (item.poisonBuff.IsValid()) {
            properties["poison"] = KCDC::Shared::Game::CryGuidToString(item.poisonBuff.low, item.poisonBuff.high);
        }
        if (item.poisonCharges != 0) {
            properties["poisonCharges"] = item.poisonCharges;
        }
        if (!item.onEquipBuffs.empty()) {
            properties["onEquipBuffs"] = nlohmann::json::array();
            for (const auto &buff : item.onEquipBuffs) {
                properties["onEquipBuffs"].push_back(KCDC::Shared::Game::CryGuidToString(buff.low, buff.high));
            }
        }
        if (!item.custom.empty()) {
            properties["custom"] = item.custom;
        }
        if (!item.customStackKey.empty()) {
            properties["customStackKey"] = item.customStackKey;
        }
        return properties;
    }

    /** A GUID given as text, into the two halves the wire carries. */
    inline bool ReadItemPropertyGuid(const nlohmann::json &value, KCDC::Shared::RPC::ItemClassId &guid) {
        return value.is_string() && KCDC::Shared::Game::CryGuidFromText(value.get_ref<const std::string &>(), guid.low, guid.high) && guid.IsValid();
    }

    /**
     * Reads encoded properties onto `item`, which is left untouched unless
     * every key is known and every value valid. Absent keys read as the
     * native creation sentinels (quality 0, health and condition -1).
     */
    inline bool ApplyItemProperties(const nlohmann::json &properties, KCDC::Shared::RPC::InventoryItem &item) {
        if (!properties.is_object()) {
            return false;
        }
        for (const auto &[key, value] : properties.items()) {
            if (key != "quality" && key != "health" && key != "condition" && key != "poison" && key != "poisonCharges" && key != "onEquipBuffs" && key != "custom" && key != "customStackKey") {
                return false;
            }
        }
        const auto quality = properties.find("quality");
        if (quality != properties.end() && (!quality->is_number_integer() || (!quality->is_number_unsigned() && quality->get<std::int64_t>() < 0) || quality->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        const auto charges = properties.find("poisonCharges");
        if (charges != properties.end() && (!charges->is_number_integer() || (!charges->is_number_unsigned() && charges->get<std::int64_t>() < 0) || charges->get<std::uint64_t>() > 10000)) {
            return false;
        }
        // nlohmann's float conversion reads booleans as zero and one; health
        // must be an actual number.
        for (const char *key : {"health", "condition"}) {
            const auto value = properties.find(key);
            if (value != properties.end() && !value->is_number()) {
                return false;
            }
        }
        auto parsed          = item;
        parsed.custom        = nlohmann::json::object();
        parsed.customStackKey.clear();
        if (properties.contains("custom") && !KCDC::Shared::Game::CustomItemRegistry::NormalizeCustom(properties.at("custom"), parsed.custom)) {
            return false;
        }
        if (properties.contains("customStackKey")) {
            if (!properties.at("customStackKey").is_string() || properties.at("customStackKey").get_ref<const std::string &>().size() != 64) {
                return false;
            }
            parsed.customStackKey = properties.at("customStackKey").get<std::string>();
        }
        parsed.quality       = quality == properties.end() ? 0U : quality->get<std::uint32_t>();
        parsed.health        = properties.contains("health") ? properties.at("health").get<float>() : -1.0F;
        parsed.condition     = properties.contains("condition") ? properties.at("condition").get<float>() : -1.0F;
        parsed.poisonBuff    = {};
        parsed.poisonCharges = charges == properties.end() ? 0U : charges->get<std::uint32_t>();
        if (const auto poison = properties.find("poison"); poison != properties.end() && !ReadItemPropertyGuid(*poison, parsed.poisonBuff)) {
            return false;
        }
        parsed.onEquipBuffs.clear();
        if (const auto buffs = properties.find("onEquipBuffs"); buffs != properties.end()) {
            if (!buffs->is_array() || buffs->size() > KCDC::Shared::RPC::kMaximumOnEquipBuffs) {
                return false;
            }
            for (const auto &value : *buffs) {
                KCDC::Shared::RPC::ItemClassId buff;
                if (!ReadItemPropertyGuid(value, buff)) {
                    return false;
                }
                parsed.onEquipBuffs.push_back(buff);
            }
            std::sort(parsed.onEquipBuffs.begin(), parsed.onEquipBuffs.end());
            if (std::adjacent_find(parsed.onEquipBuffs.begin(), parsed.onEquipBuffs.end()) != parsed.onEquipBuffs.end()) {
                return false;
            }
        }
        parsed.ownerId        = 0;
        parsed.stolenFromId   = 0;
        parsed.ownershipFlags = 0;
        parsed.phase          = -1;
        parsed.equipped       = false;
        parsed.equipmentSlot  = KCDC::Shared::RPC::kNoEquipmentSlot;
        if (!parsed.IsSane()) {
            return false;
        }
        item = parsed;
        return true;
    }
} // namespace KCDC::Shared::Inventory
