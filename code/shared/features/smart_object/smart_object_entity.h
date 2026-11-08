#pragma once

#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/game/smart_object_blueprints.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * A smart object placed where the level has none, as `SmartObject.spawn`
     * takes it, a World Builder map's `smartObjects` array holds it and
     * `smartObject.toJSON()` writes it.
     */
    struct SmartObjectDefinition {
        /** A blueprint's name, as `SmartObject.blueprints()` lists it: `Bench/bench_2places_low`. */
        std::string blueprint;

        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        /** What a script finds it by; empty for none. */
        std::string name;

        static constexpr std::size_t kMaxNameLength = 64;
    };

    inline nlohmann::json SmartObjectDefinitionToJson(const SmartObjectDefinition &object) {
        nlohmann::json entry {
            {"blueprint", object.blueprint},
            {"position", nlohmann::json::array({object.position.x, object.position.y, object.position.z})},
            {"rotation", nlohmann::json::array({object.rotation.x, object.rotation.y, object.rotation.z, object.rotation.w})},
        };
        if (!object.name.empty()) {
            entry["name"] = object.name;
        }
        return entry;
    }

    /** Reads one entry. `blueprint` and `position` are required. False, with `error` saying why, for an entry no client could build. */
    inline bool SmartObjectDefinitionFromJson(const nlohmann::json &entry, SmartObjectDefinition &object, std::string &error) {
        object = SmartObjectDefinition {};
        if (!entry.is_object()) {
            error = "a smart object must be an object";
            return false;
        }
        object.blueprint = LevelEditDefinitionJson::ReadString(entry, "blueprint");
        if (const SmartObjectBlueprint *blueprint = FindSmartObjectBlueprint(object.blueprint); blueprint == nullptr) {
            error = object.blueprint.empty() ? "a smart object needs a blueprint" : "'" + object.blueprint + "' is not a smart object blueprint; SmartObject.blueprints() lists them";
            return false;
        }
        else {
            object.blueprint = std::string(blueprint->name);
        }
        if (!LevelEditDefinitionJson::ReadVector(entry, "position", object.position)) {
            error = "position must be where it stands, as [x, y, z] or a Vector3";
            return false;
        }
        if (entry.contains("rotation") && !LevelEditDefinitionJson::ReadRotation(entry, "rotation", object.rotation)) {
            error = "rotation must be a quaternion, as [x, y, z, w] or a Quaternion";
            return false;
        }
        object.name = LevelEditDefinitionJson::ReadString(entry, "name");
        if (object.name.size() > SmartObjectDefinition::kMaxNameLength) {
            error = "name is longer than 64 characters";
            return false;
        }
        return true;
    }

    /** The row of `g_SmartObjectBlueprints` a name resolves to, or `kNoBlueprint`. */
    inline constexpr std::uint16_t kNoSmartObjectBlueprint = 0xFFFF;

    inline std::uint16_t SmartObjectBlueprintIndex(const SmartObjectBlueprint *blueprint) {
        return blueprint != nullptr ? static_cast<std::uint16_t>(blueprint - g_SmartObjectBlueprints.data()) : kNoSmartObjectBlueprint;
    }

    inline const SmartObjectBlueprint *SmartObjectBlueprintAt(std::uint16_t index) {
        return index < g_SmartObjectBlueprints.size() ? &g_SmartObjectBlueprints[index] : nullptr;
    }
} // namespace KCDC::Shared::Game

namespace KCDC::Shared::Entities {
    /**
     * A smart object the server placed: a bench, a bed, a dice table, an
     * alchemy table, a smithy, a drying rack or a smokehouse, built on every
     * client from its blueprint, part by part, under the GUIDs
     * `SmartObjectPartGuid` mints from `serial`. A seat is then named the
     * same on every machine, as the level's own are, so sitting on it, and
     * using a station, replicate as they do for those.
     *
     * Everything is construction-only: a smart object never moves, and one
     * that changes is a new one.
     */
    class SmartObjectEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::SmartObject";

        static constexpr float kStreamRange = 150.0F;

        /** A row of `g_SmartObjectBlueprints`. */
        std::uint16_t blueprint = Game::kNoSmartObjectBlueprint;

        /** What its parts' GUIDs are minted from; unique per server run. */
        std::uint32_t serial = 0;

        std::uint64_t PartGuid(std::size_t part) const {
            return Game::SmartObjectPartGuid(serial, part);
        }

        const Game::SmartObjectBlueprint *Blueprint() const {
            return Game::SmartObjectBlueprintAt(blueprint);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(blueprint);
            fields.Field(serial);
            // Full floats: a seat's sit point and a station's alignment are
            // posed from this, finer than the transform channel quantizes.
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);
            fields.Field(rotation.x);
            fields.Field(rotation.y);
            fields.Field(rotation.z);
            fields.Field(rotation.w);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &) override {}

        /** Nothing: it never moves. */
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &) override {}
    };
} // namespace KCDC::Shared::Entities
