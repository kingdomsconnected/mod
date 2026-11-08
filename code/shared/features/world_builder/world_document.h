#pragma once

#include "shared/features/area/area_definition.h"
#include "shared/features/door/door_definition.h"
#include "shared/features/ground_item/ground_item_definition.h"
#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/features/light/light_tuning.h"
#include "shared/features/patrol/patrol_route.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/smart_object/smart_object_entity.h"
#include "shared/features/vfx/vfx_tuning.h"
#include "shared/pose_math.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <unordered_set>

namespace KCDC::Shared::WorldBuilder {
    inline constexpr const char *kProjectFormat   = "kcdc.project";
    inline constexpr const char *kResourceFormat  = "kcdc.world";
    inline constexpr const char *kBlueprintFormat = "kcdc.blueprint";
    inline constexpr int kVersion                 = 2;

    inline bool SupportedVersion(const nlohmann::json &value) {
        return value == 1 || value == kVersion;
    }

    // Per array, so a hostile export cannot make the server spawn without bound.
    inline constexpr std::size_t kMaxEntries = 100000;

    /** Whether `value` is an array of exactly `count` finite numbers. */
    inline bool Numbers(const nlohmann::json &value, std::size_t count) {
        if (!value.is_array() || value.size() != count) {
            return false;
        }
        for (const nlohmann::json &number : value) {
            if (!number.is_number() || !std::isfinite(number.get<float>())) {
                return false;
            }
        }
        return true;
    }

    /** Whether `entry` carries a finite position and a non-degenerate [x, y, z, w] rotation. */
    inline bool Pose(const nlohmann::json &entry) {
        if (!entry.is_object() || !entry.contains("position") || !Numbers(entry["position"], 3) || !entry.contains("rotation") || !Numbers(entry["rotation"], 4)) {
            return false;
        }
        float length = 0.0F;
        for (const nlohmann::json &component : entry["rotation"]) {
            length += component.get<float>() * component.get<float>();
        }
        return std::isfinite(length) && length > 0.000001F;
    }

    /** An effect entry's `tuning` object. Throws for a missing key, so validate first. */
    inline Game::VfxTuning ReadTuning(const nlohmann::json &node) {
        Game::VfxTuning tuning;
        tuning.scale       = node.at("scale").get<float>();
        tuning.countScale  = node.at("count").get<float>();
        tuning.speedScale  = node.at("speed").get<float>();
        tuning.timeScale   = node.at("time").get<float>();
        tuning.strength    = node.at("strength").get<float>();
        tuning.pulsePeriod = node.at("pulse").get<float>();
        tuning.prime       = node.at("prime").get<bool>();
        return tuning;
    }

    /** Lights use the same bounded native position in documents and the runtime. */
    inline bool LightPosition(const nlohmann::json &entry) {
        if (!entry.is_object() || !entry.contains("position") || !Numbers(entry["position"], 3)) {
            return false;
        }
        return Game::IsWorldPosition(glm::vec3(entry["position"][0].get<float>(), entry["position"][1].get<float>(), entry["position"][2].get<float>()));
    }

    /** Runtime light controls; older documents omit castShadows and remain unshadowed. */
    inline bool ReadLightTuning(const nlohmann::json &entry, Game::LightTuning &tuning) {
        if (!entry.is_object() || !entry.contains("color") || !Numbers(entry["color"], 3) || !entry.contains("intensity") || !entry["intensity"].is_number() || !entry.contains("radius") || !entry["radius"].is_number()) {
            return false;
        }
        if (entry.contains("castShadows") && !entry["castShadows"].is_boolean()) {
            return false;
        }
        Game::LightTuning value;
        value.color       = glm::vec3(entry["color"][0].get<float>(), entry["color"][1].get<float>(), entry["color"][2].get<float>());
        value.intensity   = entry["intensity"].get<float>();
        value.radius      = entry["radius"].get<float>();
        value.castShadows = entry.value("castShadows", false);
        if (!value.IsSane()) {
            return false;
        }
        tuning = value;
        return true;
    }

    /** Validates a whole document before a project is replaced or anything spawns; `error` names the first bad entry. */
    inline bool ValidateContent(const nlohmann::json &content, std::string &error) {
        try {
            for (const char *key : {"objects", "effects", "world", "areas"}) {
                if (!content.contains(key) || !content[key].is_array()) {
                    error = std::string("Missing array: ") + key;
                    return false;
                }
                if (content[key].size() > kMaxEntries) {
                    error = std::string("Too many entries in ") + key;
                    return false;
                }
            }
            for (std::size_t index = 0; index < content["objects"].size(); ++index) {
                const nlohmann::json &entry = content["objects"][index];
                error                       = "Invalid prop at objects[" + std::to_string(index) + "]";
                if (!Pose(entry) || entry.at("model").get<std::string>().empty() || !Numbers(entry.at("scale"), 3)) {
                    return false;
                }
                for (const nlohmann::json &axis : entry["scale"]) {
                    if (axis.get<float>() < 0.01F || axis.get<float>() > 100.0F) {
                        return false;
                    }
                }
                Entities::PropPhysics physics = Entities::PropPhysics::Static;
                if (!Entities::ParsePropPhysics(entry.at("physics").get<std::string>(), physics)) {
                    return false;
                }
            }
            for (std::size_t index = 0; index < content["effects"].size(); ++index) {
                const nlohmann::json &entry = content["effects"][index];
                error                       = "Invalid effect at effects[" + std::to_string(index) + "]";
                if (!Pose(entry) || entry.at("effect").get<std::string>().empty() || !ReadTuning(entry.at("tuning")).IsSane()) {
                    return false;
                }
            }
            if (content.contains("lights")) {
                if (!content["lights"].is_array() || content["lights"].size() > kMaxEntries) {
                    error = "Invalid lights array";
                    return false;
                }
                for (std::size_t index = 0; index < content["lights"].size(); ++index) {
                    const auto &entry = content["lights"][index];
                    Game::LightTuning tuning;
                    error = "Invalid light at lights[" + std::to_string(index) + "]";
                    if (!LightPosition(entry) || !ReadLightTuning(entry, tuning)) {
                        return false;
                    }
                }
            }
            for (const nlohmann::json &entry : content["world"]) {
                Game::LevelEditDefinition definition;
                if (!Game::LevelEditDefinitionFromJson(entry, definition, error)) {
                    return false;
                }
            }
            // Optional, as lights are: an export from before doors had none.
            if (content.contains("doors")) {
                if (!content["doors"].is_array() || content["doors"].size() > kMaxEntries) {
                    error = "Invalid doors array";
                    return false;
                }
                for (std::size_t index = 0; index < content["doors"].size(); ++index) {
                    Game::DoorDefinition door;
                    if (!Game::DoorDefinitionFromJson(content["doors"][index], door, error)) {
                        error = "Invalid door at doors[" + std::to_string(index) + "]: " + error;
                        return false;
                    }
                }
            }
            if (content.contains("items")) {
                if (!content["items"].is_array() || content["items"].size() > kMaxEntries) {
                    error = "Invalid items array";
                    return false;
                }
                for (std::size_t index = 0; index < content["items"].size(); ++index) {
                    Game::ItemPlacementDefinition item;
                    if (!Game::ItemPlacementDefinitionFromJson(content["items"][index], item, error)) {
                        error = "Invalid item at items[" + std::to_string(index) + "]: " + error;
                        return false;
                    }
                }
            }
            if (content.contains("smartObjects")) {
                if (!content["smartObjects"].is_array() || content["smartObjects"].size() > kMaxEntries) {
                    error = "Invalid smartObjects array";
                    return false;
                }
                for (std::size_t index = 0; index < content["smartObjects"].size(); ++index) {
                    Game::SmartObjectDefinition object;
                    if (!Game::SmartObjectDefinitionFromJson(content["smartObjects"][index], object, error)) {
                        error = "Invalid smart object at smartObjects[" + std::to_string(index) + "]: " + error;
                        return false;
                    }
                }
            }
            std::unordered_set<std::string> ids;
            if (content.contains("patrolRoutes")) {
                if (!content["patrolRoutes"].is_array() || content["patrolRoutes"].size() > kMaxEntries) {
                    error = "Invalid patrolRoutes array";
                    return false;
                }
                for (const auto &entry : content["patrolRoutes"]) {
                    Game::PatrolRouteDefinition route;
                    if (!Game::PatrolRouteFromJson(entry, route, error)) {
                        return false;
                    }
                    if (!ids.insert(route.id).second) {
                        error = "Duplicate patrol route id: " + route.id;
                        return false;
                    }
                }
                ids.clear();
            }
            for (const nlohmann::json &entry : content["areas"]) {
                Game::AreaDefinition definition;
                if (!Game::AreaDefinitionFromJson(entry, definition, error)) {
                    return false;
                }
                if (!ids.insert(definition.id).second) {
                    error = "Duplicate area id: " + definition.id;
                    return false;
                }
            }
            error.clear();
            return true;
        }
        catch (const nlohmann::json::exception &exception) {
            error += ": " + std::string(exception.what());
            return false;
        }
    }

    /** Parses and validates a `.world.json` export. */
    inline bool ReadResource(const std::string &text, nlohmann::json &resource, std::string &error) {
        resource = nlohmann::json::parse(text, nullptr, false);
        if (!resource.is_object() || !resource.contains("format") || resource["format"] != kResourceFormat || !resource.contains("version") || !SupportedVersion(resource["version"])) {
            error = "Expected a kcdc.world version 1 or 2 export, not an editor project";
            return false;
        }
        if (!resource.contains("level") || !resource["level"].is_string() || resource["level"].get<std::string>().empty()) {
            error = "The export has no level";
            return false;
        }
        return ValidateContent(resource, error);
    }

    /** The runtime export of a map: its content without the editor's groups, views or preferences. */
    inline nlohmann::json ExportResource(const nlohmann::json &map) {
        nlohmann::json resource {{"format", kResourceFormat}, {"version", kVersion}, {"level", map.at("level")}};
        resource["lights"]       = map.value("lights", nlohmann::json::array());
        resource["patrolRoutes"] = map.value("patrolRoutes", nlohmann::json::array());
        resource["doors"]        = map.value("doors", nlohmann::json::array());
        resource["smartObjects"] = map.value("smartObjects", nlohmann::json::array());
        resource["items"]        = map.value("items", nlohmann::json::array());
        for (const char *key : {"objects", "effects", "world", "areas"}) {
            resource[key] = map.at(key);
        }
        for (const char *key : {"objects", "effects", "lights", "doors", "smartObjects", "items", "areas"}) {
            for (nlohmann::json &entry : resource[key]) {
                entry.erase("group");
                entry.erase("layer");
            }
        }
        return resource;
    }

    /** A stored [x, y, z] or [x, y, z, w] array as the {x, y, z[, w]} object scripts take. */
    inline nlohmann::json ScriptVector(const nlohmann::json &components) {
        nlohmann::json result {{"x", components.at(0)}, {"y", components.at(1)}, {"z", components.at(2)}};
        if (components.size() == 4) {
            result["w"] = components.at(3);
        }
        return result;
    }

    /** The server script that recreates `content` one runtime call at a time. */
    inline std::string ApiCalls(const nlohmann::json &content) {
        std::string output;
        for (const auto &entry : content.value("patrolRoutes", nlohmann::json::array())) {
            Game::PatrolRouteDefinition route;
            std::string error;
            if (Game::PatrolRouteFromJson(entry, route, error)) {
                output += "PatrolRoute.create(" + Game::PatrolRouteToJson(route, true).dump() + ");\n";
            }
        }
        for (const nlohmann::json &entry : content.at("objects")) {
            const nlohmann::json &scale      = entry.at("scale");
            const nlohmann::json scriptScale = scale[0] == scale[1] && scale[1] == scale[2] ? scale[0] : ScriptVector(scale);
            output += "Prop.spawn(" + entry.at("model").dump() + ", " + ScriptVector(entry.at("position")).dump() + ", " + ScriptVector(entry.at("rotation")).dump() + ", " + scriptScale.dump() + ", " + entry.at("physics").dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.at("effects")) {
            const nlohmann::json &tuning = entry.at("tuning");
            const nlohmann::json options {{"rotation", ScriptVector(entry.at("rotation"))}, {"scale", tuning.at("scale")}, {"countScale", tuning.at("count")}, {"speedScale", tuning.at("speed")}, {"timeScale", tuning.at("time")}, {"strength", tuning.at("strength")},
                {"pulsePeriod", tuning.at("pulse")}, {"prime", tuning.at("prime")}};
            output += "Vfx.spawn(" + entry.at("effect").dump() + ", " + ScriptVector(entry.at("position")).dump() + ", " + options.dump() + ");\n";
        }
        for (const auto &entry : content.value("lights", nlohmann::json::array())) {
            const nlohmann::json options {{"color", ScriptVector(entry.at("color"))}, {"intensity", entry.at("intensity")}, {"radius", entry.at("radius")}, {"castShadows", entry.value("castShadows", false)}};
            output += "Light.spawn(" + ScriptVector(entry.at("position")).dump() + ", " + options.dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.at("world")) {
            output += "LevelEdit.apply(" + entry.dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.value("doors", nlohmann::json::array())) {
            auto definition = entry;
            definition.erase("group");
            definition.erase("layer");
            output += "Door.spawn(" + definition.dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.value("smartObjects", nlohmann::json::array())) {
            auto definition = entry;
            definition.erase("group");
            definition.erase("layer");
            output += "SmartObject.spawn(" + definition.dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.value("items", nlohmann::json::array())) {
            Game::ItemPlacementDefinition item;
            std::string error;
            if (!Game::ItemPlacementDefinitionFromJson(entry, item, error)) {
                continue;
            }
            nlohmann::json properties = nlohmann::json::object();
            if (item.quality != 0) {
                properties["quality"] = item.quality;
            }
            if (item.health >= 0.0F) {
                properties["health"] = item.health;
            }
            if (item.condition >= 0.0F) {
                properties["condition"] = item.condition;
            }
            if (!item.name.empty()) {
                properties["name"] = item.name;
            }
            output += "GroundItem.spawn(" + nlohmann::json(item.item).dump() + ", " + ScriptVector(entry.at("position")).dump() + ", " + ScriptVector(entry.value("rotation", nlohmann::json::array({0, 0, 0, 1}))).dump() + ", " + std::to_string(item.amount) + ", undefined, "
                      + properties.dump() + ");\n";
        }
        for (const nlohmann::json &entry : content.at("areas")) {
            auto definition = entry;
            definition.erase("group");
            output += "Area.create(" + definition.dump() + ");\n";
        }
        return output;
    }
} // namespace KCDC::Shared::WorldBuilder
