#pragma once

#include "area_collision_mesh.h"
#include "area_geometry.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * One script area as `Area.create` takes it and `area.toJSON()` writes
     * it, which is also what the World Builder saves and exports. Kept here
     * so the server's `toJSON` and the editor's files are one format.
     */
    struct AreaDefinition {
        std::string id;
        std::string name;
        AreaShape shape;
        std::vector<std::string> labels;
        std::string metadataJson = "{}";
        std::optional<std::uint32_t> virtualWorld;
        bool enabled   = true;
        bool collision = false;
        AreaCollisionOptions collisionOptions;
    };

    namespace AreaDefinitionJson {
        inline nlohmann::json Vector(const glm::vec3 &value) {
            return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
        }

        inline bool ReadVector(const nlohmann::json &node, glm::vec3 &out) {
            if (!node.is_object()) {
                return false;
            }
            glm::vec3 read {0.0F};
            for (int axis = 0; axis < 3; ++axis) {
                const char *key  = axis == 0 ? "x" : axis == 1 ? "y" : "z";
                const auto found = node.find(key);
                if (found != node.end()) {
                    if (!found->is_number() || !std::isfinite(found->get<float>())) {
                        return false;
                    }
                    read[axis] = found->get<float>();
                }
            }
            out = read;
            return true;
        }

        inline bool ReadNumber(const nlohmann::json &parent, const char *key, float &out) {
            const auto found = parent.find(key);
            if (found == parent.end() || !found->is_number() || !std::isfinite(found->get<float>())) {
                return false;
            }
            out = found->get<float>();
            return true;
        }
    } // namespace AreaDefinitionJson

    inline nlohmann::json AreaDefinitionToJson(const AreaDefinition &area) {
        nlohmann::json node {
            {"id", area.id},
            {"name", area.name},
            {"type", AreaShapeTypeName(area.shape.type)},
            {"position", AreaDefinitionJson::Vector(area.shape.position)},
            {"rotation", {{"x", area.shape.rotation.x}, {"y", area.shape.rotation.y}, {"z", area.shape.rotation.z}, {"w", area.shape.rotation.w}}},
        };
        switch (area.shape.type) {
        case AreaShapeType::Box:
            node["min"] = AreaDefinitionJson::Vector(area.shape.boxMin);
            node["max"] = AreaDefinitionJson::Vector(area.shape.boxMax);
            break;
        case AreaShapeType::Shape: {
            nlohmann::json points = nlohmann::json::array();
            for (const glm::vec3 &point : area.shape.points) {
                points.push_back(AreaDefinitionJson::Vector(point));
            }
            node["points"] = std::move(points);
            node["height"] = area.shape.height;
            break;
        }
        case AreaShapeType::Sphere: node["radius"] = area.shape.radius; break;
        }
        node["labels"]                = area.labels;
        const nlohmann::json metadata = nlohmann::json::parse(area.metadataJson, nullptr, false);
        node["metadata"]              = metadata.is_object() ? metadata : nlohmann::json::object();
        if (area.virtualWorld.has_value()) {
            node["virtualWorld"] = *area.virtualWorld;
        }
        node["enabled"]            = area.enabled;
        node["collision"]          = area.collision;
        node["collisionFloor"]     = area.collisionOptions.floor;
        node["collisionCeiling"]   = area.collisionOptions.ceiling;
        node["collisionThickness"] = area.collisionOptions.thickness;
        return node;
    }

    /** Reads one definition. False, with `error`, for one `Area.create` would refuse. */
    inline bool AreaDefinitionFromJson(const nlohmann::json &node, AreaDefinition &area, std::string &error) {
        area = AreaDefinition {};
        if (!node.is_object()) {
            error = "an area definition must be an object";
            return false;
        }
        const auto id   = node.find("id");
        const auto type = node.find("type");
        if (id == node.end() || !id->is_string() || id->get<std::string>().empty()) {
            error = "an area needs a string id";
            return false;
        }
        area.id = id->get<std::string>();
        if (type == node.end() || !type->is_string() || !ParseAreaShapeType(type->get<std::string>(), area.shape.type)) {
            error = "'" + area.id + "': type must be 'box', 'shape' or 'sphere'";
            return false;
        }
        const auto name = node.find("name");
        area.name       = name != node.end() && name->is_string() ? name->get<std::string>() : area.id;

        if (const auto position = node.find("position"); position != node.end()) {
            AreaDefinitionJson::ReadVector(*position, area.shape.position);
        }
        if (const auto rotation = node.find("rotation"); rotation != node.end() && rotation->is_object()) {
            glm::quat read(1.0F, 0.0F, 0.0F, 0.0F);
            AreaDefinitionJson::ReadNumber(*rotation, "x", read.x);
            AreaDefinitionJson::ReadNumber(*rotation, "y", read.y);
            AreaDefinitionJson::ReadNumber(*rotation, "z", read.z);
            AreaDefinitionJson::ReadNumber(*rotation, "w", read.w);
            if (glm::dot(read, read) > 0.0F) {
                area.shape.rotation = glm::normalize(read);
            }
        }
        if (const auto min = node.find("min"); min != node.end()) {
            AreaDefinitionJson::ReadVector(*min, area.shape.boxMin);
        }
        if (const auto max = node.find("max"); max != node.end()) {
            AreaDefinitionJson::ReadVector(*max, area.shape.boxMax);
        }
        AreaDefinitionJson::ReadNumber(node, "height", area.shape.height);
        AreaDefinitionJson::ReadNumber(node, "radius", area.shape.radius);
        if (const auto points = node.find("points"); points != node.end() && points->is_array()) {
            for (const nlohmann::json &entry : *points) {
                glm::vec3 point {0.0F};
                if (!AreaDefinitionJson::ReadVector(entry, point)) {
                    error = "'" + area.id + "': every point must be {x, y, z}";
                    return false;
                }
                area.shape.points.push_back(point);
            }
        }
        if (!area.shape.IsValid()) {
            error = "'" + area.id + "': not a shape the engine could build";
            return false;
        }

        if (const auto labels = node.find("labels"); labels != node.end() && labels->is_array()) {
            for (const nlohmann::json &label : *labels) {
                if (label.is_string() && !label.get<std::string>().empty()) {
                    area.labels.push_back(label.get<std::string>());
                }
            }
        }
        if (const auto metadata = node.find("metadata"); metadata != node.end() && metadata->is_object()) {
            area.metadataJson = metadata->dump();
        }
        if (const auto world = node.find("virtualWorld"); world != node.end() && world->is_number_unsigned()) {
            area.virtualWorld = world->get<std::uint32_t>();
        }
        if (const auto enabled = node.find("enabled"); enabled != node.end() && enabled->is_boolean()) {
            area.enabled = enabled->get<bool>();
        }
        if (const auto collision = node.find("collision"); collision != node.end()) {
            if (!collision->is_boolean()) {
                error = "collision must be a boolean";
                return false;
            }
            area.collision = collision->get<bool>();
        }
        // Areas saved before these fields existed read as walls only.
        for (const auto &[key, plate] : {std::pair {"collisionFloor", &area.collisionOptions.floor}, std::pair {"collisionCeiling", &area.collisionOptions.ceiling}}) {
            if (const auto found = node.find(key); found != node.end()) {
                if (!found->is_boolean()) {
                    error = std::string(key) + " must be a boolean";
                    return false;
                }
                *plate = found->get<bool>();
            }
        }
        if (const auto thickness = node.find("collisionThickness"); thickness != node.end()) {
            if (!thickness->is_number()) {
                error = "collisionThickness must be a number";
                return false;
            }
            area.collisionOptions.thickness = thickness->get<float>();
        }
        if (!area.collisionOptions.IsValid()) {
            error = AreaCollisionOptions::ThicknessRangeError();
            return false;
        }
        if (area.collision) {
            AreaCollisionMesh mesh;
            if (!BuildAreaCollisionMesh(area.shape, area.collisionOptions, mesh, error)) {
                return false;
            }
        }
        return true;
    }

    /** A JSON array of definitions, which a script can loop over into `Area.create`. */
    inline std::string SerializeAreaDefinitions(const std::vector<AreaDefinition> &areas) {
        nlohmann::json array = nlohmann::json::array();
        for (const AreaDefinition &area : areas) {
            array.push_back(AreaDefinitionToJson(area));
        }
        return array.dump(2);
    }

    /** Reads an array of definitions, or a single one. Counts the ones it could not read in `skipped`; false only when it is not JSON at all. */
    inline bool ParseAreaDefinitions(const std::string &text, std::vector<AreaDefinition> &areas, std::size_t &skipped, std::string &error) {
        areas.clear();
        skipped                   = 0;
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded() || (!root.is_array() && !root.is_object())) {
            error = "that is not a JSON area definition or array of them";
            return false;
        }
        const auto read = [&](const nlohmann::json &node) {
            AreaDefinition area;
            std::string ignored;
            if (AreaDefinitionFromJson(node, area, ignored)) {
                areas.push_back(std::move(area));
            }
            else {
                ++skipped;
            }
        };
        if (root.is_array()) {
            for (const nlohmann::json &node : root) {
                read(node);
            }
        }
        else {
            read(root);
        }
        return true;
    }
} // namespace KCDC::Shared::Game
