#pragma once

#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/game/inventory_property_classes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * A pickable stack laid in the world by hand, as a World Builder map's
     * `items` array holds it. The server lays it down with
     * `GroundItem.spawn`'s rules: already at rest, picked up whole.
     */
    struct ItemPlacementDefinition {
        /** The item class, by GUID or by name, as `GroundItem.spawn` takes it. */
        std::string item;

        std::uint32_t amount = 1;

        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        /** What a script finds it by; empty for none. */
        std::string name;

        /** The game's grade, or 0 for the class's own. */
        std::uint32_t quality = 0;

        /** 0 to 1, or negative for the class's own. */
        float health    = -1.0F;
        float condition = -1.0F;

        static constexpr std::size_t kMaxNameLength = 64;
        static constexpr std::uint32_t kMaxAmount   = 10000;
    };

    inline nlohmann::json ItemPlacementDefinitionToJson(const ItemPlacementDefinition &item) {
        nlohmann::json entry {
            {"item", item.item},
            {"amount", item.amount},
            {"position", nlohmann::json::array({item.position.x, item.position.y, item.position.z})},
            {"rotation", nlohmann::json::array({item.rotation.x, item.rotation.y, item.rotation.z, item.rotation.w})},
        };
        if (!item.name.empty()) {
            entry["name"] = item.name;
        }
        if (item.quality != 0) {
            entry["quality"] = item.quality;
        }
        if (item.health >= 0.0F) {
            entry["health"] = item.health;
        }
        if (item.condition >= 0.0F) {
            entry["condition"] = item.condition;
        }
        return entry;
    }

    /**
     * Reads one entry. `item` and `position` are required. The item class is
     * resolved where it is spawned, against the classes that server knows,
     * custom and streamed ones included. False, with `error` saying why, for
     * an entry that describes no stack.
     */
    inline bool ItemPlacementDefinitionFromJson(const nlohmann::json &entry, ItemPlacementDefinition &item, std::string &error) {
        item = ItemPlacementDefinition {};
        if (!entry.is_object()) {
            error = "an item must be an object";
            return false;
        }
        item.item = LevelEditDefinitionJson::ReadString(entry, "item");
        if (item.item.empty() || item.item.size() > 128) {
            error = "an item needs its class, by GUID or by name";
            return false;
        }
        if (!LevelEditDefinitionJson::ReadVector(entry, "position", item.position)) {
            error = "position must be where it lies, as [x, y, z] or a Vector3";
            return false;
        }
        if (entry.contains("rotation") && !LevelEditDefinitionJson::ReadRotation(entry, "rotation", item.rotation)) {
            error = "rotation must be a quaternion, as [x, y, z, w] or a Quaternion";
            return false;
        }
        if (const auto amount = entry.find("amount"); amount != entry.end()) {
            if (!amount->is_number_integer() || amount->get<std::int64_t>() < 1 || amount->get<std::int64_t>() > ItemPlacementDefinition::kMaxAmount) {
                error = "amount must be a whole number from 1 to 10000";
                return false;
            }
            item.amount = amount->get<std::uint32_t>();
        }
        if (const auto quality = entry.find("quality"); quality != entry.end()) {
            if (!quality->is_number_integer() || quality->get<std::int64_t>() < 0 || quality->get<std::int64_t>() > 255) {
                error = "quality must be a whole number from 0 to 255";
                return false;
            }
            item.quality = quality->get<std::uint32_t>();
        }
        for (const auto &[key, target] : {std::pair<const char *, float *> {"health", &item.health}, std::pair<const char *, float *> {"condition", &item.condition}}) {
            if (const auto value = entry.find(key); value != entry.end()) {
                if (!value->is_number() || !std::isfinite(value->get<float>()) || value->get<float>() < 0.0F || value->get<float>() > 1.0F) {
                    error = std::string(key) + " must be a number from 0 to 1";
                    return false;
                }
                *target = value->get<float>();
            }
        }
        item.name = LevelEditDefinitionJson::ReadString(entry, "name");
        if (item.name.size() > ItemPlacementDefinition::kMaxNameLength) {
            error = "name is longer than 64 characters";
            return false;
        }
        // A shipped class named by GUID is checked here; one named otherwise is the server's to resolve and judge.
        if (const InventoryPropertyClass *properties = FindInventoryPropertyClass(item.item); properties != nullptr) {
            if (properties->questItem) {
                error = "a quest item cannot lie on the ground";
                return false;
            }
            if (item.amount > 1 && !properties->nativeStackable) {
                error = "this item does not stack: place an amount of 1 per stack";
                return false;
            }
        }
        return true;
    }
} // namespace KCDC::Shared::Game
