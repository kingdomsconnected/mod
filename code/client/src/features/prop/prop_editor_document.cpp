#include <utils/streamed_assets/policy.h>

#include "prop_editor_document.h"

#include "core/ui/native_ui/native_ui_assets.h"

#include "shared/features/world_builder/world_document.h"
#include <Windows.h>
#include <commdlg.h>
#include <nlohmann/json.hpp>
#include <utils/string_utils.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <tuple>
#include <unordered_set>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kFormat = "kcdc.map";
        constexpr int kVersion        = 2;

        HWND DialogOwner() {
            if (const HWND active = GetActiveWindow()) {
                return active;
            }
            // The editor renders on a different thread from the game window.
            const HWND foreground = GetForegroundWindow();
            DWORD process         = 0;
            GetWindowThreadProcessId(foreground, &process);
            return process == GetCurrentProcessId() ? foreground : nullptr;
        }

        nlohmann::json Vec3ToJson(const SDK::Vec3 &value) {
            return nlohmann::json::array({value.x, value.y, value.z});
        }

        nlohmann::json QuatToJson(const SDK::Quat &value) {
            return nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        /** Rejects non-finite numbers, so a hand-edited file cannot put NaN into a transform. */
        bool ReadFloats(const nlohmann::json &node, std::size_t count, float *out) {
            if (!node.is_array() || node.size() != count) {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index) {
                if (!node[index].is_number()) {
                    return false;
                }
                out[index] = node[index].get<float>();
                if (!std::isfinite(out[index])) {
                    return false;
                }
            }
            return true;
        }

        bool ReadVec3(const nlohmann::json &parent, const char *key, SDK::Vec3 &out) {
            const auto found = parent.find(key);
            float values[3] {};
            if (found == parent.end() || !ReadFloats(*found, 3, values)) {
                return false;
            }
            out = SDK::Vec3 {values[0], values[1], values[2]};
            return true;
        }

        bool ReadQuat(const nlohmann::json &parent, const char *key, SDK::Quat &out) {
            const auto found = parent.find(key);
            float values[4] {};
            if (found == parent.end() || !ReadFloats(*found, 4, values)) {
                return false;
            }
            out = SDK::Quat {values[0], values[1], values[2], values[3]};
            return true;
        }

        std::string ReadString(const nlohmann::json &parent, const char *key) {
            const auto found = parent.find(key);
            return found != parent.end() && found->is_string() ? found->get<std::string>() : std::string {};
        }

        bool ReadBool(const nlohmann::json &parent, const char *key) {
            const auto found = parent.find(key);
            return found != parent.end() && found->is_boolean() && found->get<bool>();
        }

        // A project or blueprint file: `format` and `version` around a `scene` map.
        bool IsWrapper(const nlohmann::json &root, const char *format) {
            return root.is_object() && ReadString(root, "format") == format && root.contains("version") && Shared::WorldBuilder::SupportedVersion(root["version"]) && root.contains("scene");
        }
    } // namespace

    nlohmann::json MapDocumentToJson(const MapDocument &document) {
        nlohmann::json objects = nlohmann::json::array();
        // A door, smart object or item goes in its own array, in the format
        // the server's Door.spawn, SmartObject.spawn and resource items read.
        nlohmann::json placed {{"doors", nlohmann::json::array()}, {"smartObjects", nlohmann::json::array()}, {"items", nlohmann::json::array()}};
        for (const DocumentObject &object : document.objects) {
            if (object.kind != PlacedKind::Prop) {
                nlohmann::json entry = WorldObjectToJson(object.kind, object.model, object.settings, object.position, object.rotation);
                if (!object.group.empty()) {
                    entry["group"] = object.group;
                }
                placed[WorldObjectArray(object.kind)].push_back(std::move(entry));
                continue;
            }
            objects.push_back({
                {"model", object.model},
                {"position", Vec3ToJson(object.position)},
                {"rotation", QuatToJson(object.rotation)},
                {"scale", Vec3ToJson(object.scale)},
                {"physics", KCDC::Common::Spawn::PropPhysicsToString(object.physics)},
                {"group", object.group},
            });
        }

        nlohmann::json effects = nlohmann::json::array();
        for (const DocumentEffect &effect : document.effects) {
            effects.push_back({
                {"effect", effect.effect},
                {"group", effect.group},
                {"position", Vec3ToJson(effect.position)},
                {"rotation", QuatToJson(effect.rotation)},
                {"tuning",
                    {
                        {"scale", effect.tuning.scale},
                        {"count", effect.tuning.countScale},
                        {"speed", effect.tuning.speedScale},
                        {"time", effect.tuning.timeScale},
                        {"strength", effect.tuning.strength},
                        {"pulse", effect.tuning.pulsePeriod},
                        {"prime", effect.tuning.prime},
                    }},
            });
        }

        nlohmann::json lights = nlohmann::json::array();
        for (const DocumentLight &light : document.lights) {
            lights.push_back(
                {{"position", Vec3ToJson(light.position)}, {"color", {light.tuning.color.x, light.tuning.color.y, light.tuning.color.z}}, {"intensity", light.tuning.intensity}, {"radius", light.tuning.radius}, {"castShadows", light.tuning.castShadows}, {"group", light.group}});
        }

        // The shared writer, so a map's `world` array is exactly what the
        // server's LevelEdit.apply and world resources read.
        nlohmann::json world = nlohmann::json::array();
        for (const Shared::Game::LevelEditDefinition &edit : document.world) {
            world.push_back(Shared::Game::LevelEditDefinitionToJson(edit));
        }

        nlohmann::json views = nlohmann::json::array();
        for (const DocumentView &view : document.views) {
            views.push_back({
                {"name", view.name},
                {"position", Vec3ToJson(view.position)},
                {"yaw", view.yawDegrees},
                {"pitch", view.pitchDegrees},
            });
        }

        nlohmann::json areas = nlohmann::json::array();
        for (const Shared::Game::AreaDefinition &area : document.areas) {
            auto entry = Shared::Game::AreaDefinitionToJson(area);
            if (const auto group = document.areaGroups.find(area.id); group != document.areaGroups.end() && !group->second.empty()) {
                entry["group"] = group->second;
            }
            areas.push_back(std::move(entry));
        }

        nlohmann::json patrolRoutes = nlohmann::json::array();
        for (const auto &route : document.patrolRoutes) {
            patrolRoutes.push_back(Shared::Game::PatrolRouteToJson(route));
        }

        nlohmann::json map {
            {"patrolRoutes", std::move(patrolRoutes)},
            {"format", kFormat},
            {"version", kVersion},
            {"level", document.level},
            {"objects", std::move(objects)},
            {"effects", std::move(effects)},
            {"lights", std::move(lights)},
            {"world", std::move(world)},
            {"views", std::move(views)},
            {"areas", std::move(areas)},
        };
        // Only when there are any, so a map without them reads as it always did.
        for (auto &[key, entries] : placed.items()) {
            if (!entries.empty()) {
                map[key] = std::move(entries);
            }
        }
        return map;
    }

    std::string SerializeMapDocument(const MapDocument &document) {
        return MapDocumentToJson(document).dump(2);
    }

    bool ParseMapDocument(const std::string &text, MapDocument &document, std::size_t &skipped, std::string &error) {
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded()) {
            document = MapDocument {};
            skipped  = 0;
            error    = "that is not JSON";
            return false;
        }
        return MapDocumentFromJson(root, document, skipped, error);
    }

    bool MapDocumentFromJson(const nlohmann::json &root, MapDocument &document, std::size_t &skipped, std::string &error) {
        document = MapDocument {};
        skipped  = 0;

        if (!root.is_object()) {
            error = "that is not a JSON object";
            return false;
        }

        const std::string format = ReadString(root, "format");
        if (!format.empty() && format != kFormat) {
            error = "that is a '" + format + "' document, not a map";
            return false;
        }

        const auto objects = root.find("objects");
        if (objects == root.end() || !objects->is_array()) {
            error = "no `objects` array in that document";
            return false;
        }

        document.level = ReadString(root, "level");

        for (const nlohmann::json &entry : *objects) {
            DocumentObject object;
            if (!entry.is_object() || (object.model = ReadString(entry, "model")).empty() || !ReadVec3(entry, "position", object.position)) {
                ++skipped;
                continue;
            }
            ReadQuat(entry, "rotation", object.rotation);
            ReadVec3(entry, "scale", object.scale);
            object.physics = KCDC::Common::Spawn::PropPhysicsFromString(ReadString(entry, "physics")).value_or(KCDC::Common::Spawn::PropPhysics::Static);
            object.group   = ReadString(entry, "group");
            document.objects.push_back(std::move(object));
        }

        // After the meshes, kind by kind, which is the order the writer and
        // a project's layer arrays keep them in.
        for (const PlacedKind kind : {PlacedKind::Door, PlacedKind::SmartObject, PlacedKind::Item}) {
            const auto entries = root.find(WorldObjectArray(kind));
            if (entries == root.end() || !entries->is_array()) {
                continue;
            }
            for (const nlohmann::json &entry : *entries) {
                DocumentObject object;
                std::string ignored;
                object.kind = kind;
                if (!WorldObjectFromJson(kind, entry, object.model, object.settings, object.position, object.rotation, ignored)) {
                    ++skipped;
                    continue;
                }
                object.group = ReadString(entry, "group");
                document.objects.push_back(std::move(object));
            }
        }

        if (const auto effects = root.find("effects"); effects != root.end() && effects->is_array()) {
            for (const nlohmann::json &entry : *effects) {
                DocumentEffect effect;
                if (!entry.is_object() || (effect.effect = ReadString(entry, "effect")).empty() || !ReadVec3(entry, "position", effect.position)) {
                    ++skipped;
                    continue;
                }
                ReadQuat(entry, "rotation", effect.rotation);
                effect.group = ReadString(entry, "group");
                if (const auto tuning = entry.find("tuning"); tuning != entry.end() && tuning->is_object()) {
                    const auto number = [&tuning](const char *key, float fallback) {
                        const auto found = tuning->find(key);
                        return found != tuning->end() && found->is_number() && std::isfinite(found->get<float>()) ? found->get<float>() : fallback;
                    };
                    effect.tuning.scale       = number("scale", 1.0F);
                    effect.tuning.countScale  = number("count", 1.0F);
                    effect.tuning.speedScale  = number("speed", 1.0F);
                    effect.tuning.timeScale   = number("time", 1.0F);
                    effect.tuning.strength    = number("strength", -1.0F);
                    effect.tuning.pulsePeriod = number("pulse", 0.0F);
                    effect.tuning.prime       = ReadBool(*tuning, "prime");
                }
                if (!effect.tuning.IsSane()) {
                    ++skipped;
                    continue;
                }
                document.effects.push_back(std::move(effect));
            }
        }

        if (const auto lights = root.find("lights"); lights != root.end()) {
            if (!lights->is_array() || lights->size() > Shared::WorldBuilder::kMaxEntries) {
                error = "Invalid lights array";
                return false;
            }
            for (const auto &entry : *lights) {
                DocumentLight light;
                if (!Shared::WorldBuilder::LightPosition(entry) || !ReadVec3(entry, "position", light.position) || !Shared::WorldBuilder::ReadLightTuning(entry, light.tuning)) {
                    ++skipped;
                    continue;
                }
                light.group = ReadString(entry, "group");
                document.lights.push_back(std::move(light));
            }
        }

        if (const auto world = root.find("world"); world != root.end() && world->is_array()) {
            for (const nlohmann::json &entry : *world) {
                Shared::Game::LevelEditDefinition edit;
                std::string ignored;
                if (!Shared::Game::LevelEditDefinitionFromJson(entry, edit, ignored)) {
                    ++skipped;
                    continue;
                }
                document.world.push_back(std::move(edit));
            }
        }

        if (const auto views = root.find("views"); views != root.end() && views->is_array()) {
            for (const nlohmann::json &entry : *views) {
                DocumentView view;
                if (!entry.is_object() || !ReadVec3(entry, "position", view.position)) {
                    ++skipped;
                    continue;
                }
                view.name = ReadString(entry, "name");
                if (const auto yaw = entry.find("yaw"); yaw != entry.end() && yaw->is_number()) {
                    view.yawDegrees = yaw->get<float>();
                }
                if (const auto pitch = entry.find("pitch"); pitch != entry.end() && pitch->is_number()) {
                    view.pitchDegrees = pitch->get<float>();
                }
                document.views.push_back(std::move(view));
            }
        }

        if (const auto routes = root.find("patrolRoutes"); routes != root.end() && routes->is_array()) {
            for (const auto &entry : *routes) {
                Shared::Game::PatrolRouteDefinition route;
                std::string ignored;
                if (Shared::Game::PatrolRouteFromJson(entry, route, ignored)) {
                    document.patrolRoutes.push_back(std::move(route));
                }
                else {
                    ++skipped;
                }
            }
        }
        if (const auto areas = root.find("areas"); areas != root.end() && areas->is_array()) {
            for (const nlohmann::json &entry : *areas) {
                Shared::Game::AreaDefinition area;
                std::string ignored;
                if (!Shared::Game::AreaDefinitionFromJson(entry, area, ignored)) {
                    ++skipped;
                    continue;
                }
                if (const auto group = ReadString(entry, "group"); !group.empty()) {
                    document.areaGroups.emplace(area.id, group);
                }
                document.areas.push_back(std::move(area));
            }
        }

        return true;
    }

    const std::string &MapFiles::Directory() {
        // Once: the panel asks on every hover of the save button.
        static const std::string directory = [] {
            const std::string &module = KCDC::Core::NativeUIAssets::ModuleDirectory();
            if (module.empty()) {
                return std::string {};
            }
            const std::filesystem::path path = std::filesystem::path(module) / "maps";
            std::error_code code;
            std::filesystem::create_directories(path, code);
            return path.string();
        }();
        return directory;
    }

    std::string MapFiles::SanitizeName(const std::string &name) {
        std::string clean;
        clean.reserve(name.size());
        for (const char character : name) {
            const auto byte = static_cast<unsigned char>(character);
            clean.push_back(std::isalnum(byte) != 0 || character == '-' || character == '_' ? character : '_');
        }
        return clean.empty() ? std::string {"untitled"} : clean;
    }

    std::vector<std::string> ParseRecentProjects(const std::string &text) {
        const auto entries = nlohmann::json::parse(text, nullptr, false);
        std::vector<std::string> paths;
        if (!entries.is_array()) {
            return paths;
        }
        for (const auto &entry : entries) {
            if (!entry.is_string()) {
                continue;
            }
            const auto path = entry.get<std::string>();
            if (!path.empty() && path.find('\0') == std::string::npos && std::find(paths.begin(), paths.end(), path) == paths.end()) {
                paths.push_back(path);
                if (paths.size() == 10) {
                    break;
                }
            }
        }
        return paths;
    }

    std::string MapFiles::RecentProjectsPath() {
        return Directory() + "/projects.prefs";
    }

    std::vector<std::string> MapFiles::RecentProjects() {
        std::string text, error;
        return ReadPath(RecentProjectsPath(), text, error) ? ParseRecentProjects(text) : std::vector<std::string> {};
    }

    nlohmann::json ProjectToJson(const MapDocument &document) {
        nlohmann::json project {
            {"format", Shared::WorldBuilder::kProjectFormat},
            {"version", Shared::WorldBuilder::kVersion},
            {"scene", MapDocumentToJson(document)},
            {"editor", {{"exportDirectory", document.exportDirectory}, {"resourceName", document.resourceName}}},
        };
        if (!document.clientModsFolder.empty() || !document.clientMods.empty()) {
            project["editor"]["clientMods"] = {{"folder", document.clientModsFolder}, {"enabled", document.clientMods}};
        }
        auto layers       = document.layers.ToJson(false);
        layers["objects"] = nlohmann::json::array();
        layers["effects"] = nlohmann::json::array();
        layers["lights"]  = nlohmann::json::array();
        layers["world"]   = nlohmann::json::array();
        // Parallel to each scene array, so a door's layer sits at its index in `doors`.
        for (const auto &object : document.objects) {
            const char *key = object.kind == PlacedKind::Prop ? "objects" : WorldObjectArray(object.kind);
            if (!layers.contains(key)) {
                layers[key] = nlohmann::json::array();
            }
            layers[key].push_back(object.layer);
        }
        for (const auto &effect : document.effects) {
            layers["effects"].push_back(effect.layer);
        }
        for (const auto &light : document.lights) {
            layers["lights"].push_back(light.layer);
        }
        for (const auto &member : document.worldLayers) {
            layers["world"].push_back({{"object", Shared::Game::LevelEditDefinitionToJson(member.object)}, {"layer", member.layer}});
        }
        project["editor"]["layers"]      = std::move(layers);
        project["editor"]["viewport"]    = document.viewport.ToJson();
        project["editor"]["environment"] = document.environment.ToJson();
        project["editor"]["favorites"]   = {{"props", document.propFavorites.ToJson()}, {"effects", document.effectFavorites.ToJson()}};
        if (document.camera) {
            project["editor"]["camera"] = {{"position", Vec3ToJson(document.camera->position)}, {"yaw", document.camera->yawDegrees}, {"pitch", document.camera->pitchDegrees}};
        }
        return project;
    }

    std::string SerializeProject(const MapDocument &document) {
        return ProjectToJson(document).dump(2);
    }

    bool ParseProject(const std::string &text, MapDocument &destination, std::string &error) {
        MapDocument document;
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (!IsWrapper(root, Shared::WorldBuilder::kProjectFormat)) {
            error = "Choose a World Builder project (.project.json), not an exported world resource";
            return false;
        }
        const nlohmann::json &scene = root["scene"];
        if (!Shared::WorldBuilder::ValidateContent(scene, error)) {
            return false;
        }
        std::size_t skipped = 0;
        if (!MapDocumentFromJson(scene, document, skipped, error) || skipped != 0) {
            error = "The project contains malformed entries: " + error;
            return false;
        }
        if (root.contains("editor") && !root["editor"].is_object()) {
            error = "Project editor preferences must be an object.";
            return false;
        }
        if (const auto editor = root.find("editor"); editor != root.end() && editor->is_object()) {
            if (editor->contains("viewport") && !EditorViewport::FromJson(editor->at("viewport"), document.viewport, error)) {
                return false;
            }
            if (editor->contains("environment") && !EnvironmentPreview::FromJson(editor->at("environment"), document.environment, error)) {
                return false;
            }
            if (editor->contains("favorites")) {
                const auto &favorites = editor->at("favorites");
                if (!favorites.is_object() || !favorites.contains("props") || !favorites.contains("effects")) {
                    error = "Invalid project favorites.";
                    return false;
                }
                if (!FavoriteFolders::FromJson(favorites.at("props"), document.propFavorites, error) || !FavoriteFolders::FromJson(favorites.at("effects"), document.effectFavorites, error)) {
                    return false;
                }
            }
            if (editor->contains("layers")) {
                const auto &layers = editor->at("layers");
                if (layers.contains("members")) {
                    error = "Project layers cannot contain session object IDs.";
                    return false;
                }
                if (!EditorLayers::FromJson(layers, document.layers, error)) {
                    return false;
                }
                std::size_t customMembers = 0;
                const auto readLayers     = [&](const char *key, auto &entries) {
                    if (!layers.contains(key) || !layers[key].is_array() || layers[key].size() != entries.size()) {
                        return false;
                    }
                    for (std::size_t i = 0; i < entries.size(); ++i) {
                        if (!layers[key][i].is_string() || !document.layers.Find(layers[key][i].template get<std::string>())) {
                            return false;
                        }
                        entries[i].layer = layers[key][i].template get<std::string>();
                        customMembers += entries[i].layer != "default" ? 1 : 0;
                    }
                    return true;
                };
                const auto readObjectLayers = [&](PlacedKind kind) {
                    std::vector<DocumentObject *> members;
                    for (DocumentObject &object : document.objects) {
                        if (object.kind == kind) {
                            members.push_back(&object);
                        }
                    }
                    const char *key = kind == PlacedKind::Prop ? "objects" : WorldObjectArray(kind);
                    // Optional for a kind the project has none of, as lights are: a project from before it had none.
                    if (kind != PlacedKind::Prop && members.empty() && !layers.contains(key)) {
                        return true;
                    }
                    if (!layers.contains(key) || !layers[key].is_array() || layers[key].size() != members.size()) {
                        return false;
                    }
                    for (std::size_t i = 0; i < members.size(); ++i) {
                        if (!layers[key][i].is_string() || !document.layers.Find(layers[key][i].template get<std::string>())) {
                            return false;
                        }
                        members[i]->layer = layers[key][i].template get<std::string>();
                        customMembers += members[i]->layer != "default" ? 1 : 0;
                    }
                    return true;
                };
                const bool objectsRead = readObjectLayers(PlacedKind::Prop) && readObjectLayers(PlacedKind::Door) && readObjectLayers(PlacedKind::SmartObject) && readObjectLayers(PlacedKind::Item);
                if (!objectsRead || !readLayers("effects", document.effects) || ((layers.contains("lights") || !document.lights.empty()) && !readLayers("lights", document.lights))) {
                    error = "Invalid object layer assignment.";
                    return false;
                }
                if (customMembers > EditorLayers::kMaxMembers) {
                    error = "Too many layer members.";
                    return false;
                }
                if (!layers.contains("world") || !layers["world"].is_array() || layers["world"].size() > Shared::WorldBuilder::kMaxEntries) {
                    error = "Invalid world layer members.";
                    return false;
                }
                std::unordered_set<std::uint64_t> entityIdentities;
                std::map<std::tuple<std::string, double, double, double>, std::vector<glm::vec3>> brushCells;
                for (const auto &member : layers["world"]) {
                    DocumentWorldLayer value;
                    if (!member.is_object() || !member.contains("object") || !member.contains("layer") || !member["layer"].is_string()) {
                        error = "Invalid world layer member.";
                        return false;
                    }
                    value.layer = member["layer"].get<std::string>();
                    if (!document.layers.Find(value.layer) || !Shared::Game::LevelEditDefinitionFromJson(member["object"], value.object, error)) {
                        error = "Invalid world layer identity or layer.";
                        return false;
                    }
                    customMembers += value.layer != "default" ? 1 : 0;
                    if (customMembers > EditorLayers::kMaxMembers) {
                        error = "Too many layer members.";
                        return false;
                    }
                    if (value.object.hidden || value.object.moved) {
                        error = "Layer membership must not change the world object.";
                        return false;
                    }
                    bool duplicate = false;
                    if (value.object.kind == Shared::Game::LevelEditKind::Entity) {
                        duplicate = !entityIdentities.insert(value.object.guid).second;
                    }
                    else {
                        // Match the owner's case-insensitive path and spatial key,
                        // regardless of the JSON spelling of the same position.
                        const auto path           = Framework::Utils::StringUtils::ToLower(value.object.name);
                        constexpr double cellSize = 2.0 * Shared::Game::kLevelEditKeySlack;
                        const double cellX        = std::floor(static_cast<double>(value.object.at.x) / cellSize);
                        const double cellY        = std::floor(static_cast<double>(value.object.at.y) / cellSize);
                        const double cellZ        = std::floor(static_cast<double>(value.object.at.z) / cellSize);
                        for (int x = -1; x <= 1 && !duplicate; ++x) {
                            for (int y = -1; y <= 1 && !duplicate; ++y) {
                                for (int z = -1; z <= 1 && !duplicate; ++z) {
                                    const auto cell = brushCells.find({path, cellX + x, cellY + y, cellZ + z});
                                    if (cell == brushCells.end()) {
                                        continue;
                                    }
                                    for (const auto &position : cell->second) {
                                        const auto offset = position - value.object.at;
                                        if (glm::dot(offset, offset) <= Shared::Game::kLevelEditKeySlack * Shared::Game::kLevelEditKeySlack) {
                                            duplicate = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (!duplicate) {
                            brushCells[{path, cellX, cellY, cellZ}].push_back(value.object.at);
                        }
                    }
                    if (duplicate) {
                        error = "Duplicate world layer member.";
                        return false;
                    }
                    document.worldLayers.push_back(std::move(value));
                }
            }
            document.exportDirectory = ReadString(*editor, "exportDirectory");
            if (const auto clientMods = editor->find("clientMods"); clientMods != editor->end()) {
                if (!clientMods->is_object() || !clientMods->contains("folder") || !clientMods->at("folder").is_string() || clientMods->at("folder").get_ref<const std::string &>().size() >= 1024 || !clientMods->contains("enabled") || !clientMods->at("enabled").is_array()) {
                    error = "Invalid project client mods.";
                    return false;
                }
                document.clientModsFolder = clientMods->at("folder").get<std::string>();
                for (const nlohmann::json &name : clientMods->at("enabled")) {
                    if (!name.is_string() || !Framework::Utils::StreamedAssets::IsValidResourceName(name.get_ref<const std::string &>())) {
                        error = "Invalid project client mods.";
                        return false;
                    }
                    document.clientMods.push_back(name.get<std::string>());
                }
                std::sort(document.clientMods.begin(), document.clientMods.end());
            }
            if (const std::string resourceName = ReadString(*editor, "resourceName"); !resourceName.empty()) {
                document.resourceName = resourceName;
            }
            if (const auto camera = editor->find("camera"); camera != editor->end()) {
                DocumentView pose;
                if (!camera->is_object() || !ReadVec3(*camera, "position", pose.position) || !camera->contains("yaw") || !(*camera)["yaw"].is_number() || !camera->contains("pitch") || !(*camera)["pitch"].is_number()) {
                    error = "The project camera needs a position, yaw and pitch.";
                    return false;
                }
                pose.yawDegrees   = (*camera)["yaw"].get<float>();
                pose.pitchDegrees = (*camera)["pitch"].get<float>();
                if (!std::isfinite(pose.yawDegrees) || !std::isfinite(pose.pitchDegrees) || std::fabs(pose.pitchDegrees) > 89.0F) {
                    error = "The project camera has invalid angles.";
                    return false;
                }
                document.camera = pose;
            }
        }
        destination = std::move(document);
        return true;
    }

    void OffsetDocument(MapDocument &document, const SDK::Vec3 &offset) {
        for (DocumentObject &object : document.objects) {
            object.position.x += offset.x;
            object.position.y += offset.y;
            object.position.z += offset.z;
        }
        for (DocumentEffect &effect : document.effects) {
            effect.position.x += offset.x;
            effect.position.y += offset.y;
            effect.position.z += offset.z;
        }
        for (DocumentLight &light : document.lights) {
            light.position.x += offset.x;
            light.position.y += offset.y;
            light.position.z += offset.z;
        }
        for (auto &route : document.patrolRoutes) {
            for (auto &point : route.points) {
                point.position += glm::vec3(offset.x, offset.y, offset.z);
            }
        }
        for (Shared::Game::AreaDefinition &area : document.areas) {
            area.shape.position += glm::vec3(offset.x, offset.y, offset.z);
        }
    }

    bool SerializeBlueprint(MapDocument document, std::string &text, std::string &error) {
        if (!document.world.empty()) {
            error = "Level edits belong to their original level and cannot be saved in a blueprint.";
            return false;
        }
        if (document.objects.empty() && document.effects.empty() && document.lights.empty() && document.areas.empty() && document.patrolRoutes.empty()) {
            error = "Select something to save first.";
            return false;
        }

        // The first member is the pivot, so placing the blueprint puts it where the camera looks.
        SDK::Vec3 pivot {};
        if (!document.objects.empty()) {
            pivot = document.objects.front().position;
        }
        else if (!document.effects.empty()) {
            pivot = document.effects.front().position;
        }
        else if (!document.lights.empty()) {
            pivot = document.lights.front().position;
        }
        else {
            const glm::vec3 &position = !document.areas.empty() ? document.areas.front().shape.position : document.patrolRoutes.front().points.front().position;
            pivot                     = SDK::Vec3 {position.x, position.y, position.z};
        }
        OffsetDocument(document, SDK::Vec3 {-pivot.x, -pivot.y, -pivot.z});
        document.level.clear();
        document.views.clear();

        nlohmann::json scene = MapDocumentToJson(document);
        if (!Shared::WorldBuilder::ValidateContent(scene, error)) {
            return false;
        }
        text = nlohmann::json {{"format", Shared::WorldBuilder::kBlueprintFormat}, {"version", Shared::WorldBuilder::kVersion}, {"scene", std::move(scene)}}.dump(2);
        return true;
    }

    bool ParseBlueprint(const std::string &text, MapDocument &document, std::string &error) {
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (!IsWrapper(root, Shared::WorldBuilder::kBlueprintFormat)) {
            error = "Choose a World Builder blueprint (.blueprint.json).";
            return false;
        }
        const nlohmann::json &scene = root["scene"];
        if (!Shared::WorldBuilder::ValidateContent(scene, error)) {
            return false;
        }
        std::size_t skipped = 0;
        if (!MapDocumentFromJson(scene, document, skipped, error) || skipped != 0 || !document.world.empty()) {
            error = "Blueprint contains invalid or level-specific entries.";
            return false;
        }
        if (document.objects.empty() && document.effects.empty() && document.lights.empty() && document.areas.empty() && document.patrolRoutes.empty()) {
            error = "The blueprint is empty.";
            return false;
        }
        return true;
    }

    bool MapFiles::WritePath(const std::string &filename, const std::string &text, std::string &error) {
        const auto path = std::filesystem::u8path(filename);
        if (filename.empty()) {
            error = "Choose a file first";
            return false;
        }
        std::error_code code;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), code);
        }
        if (code) {
            error = "Cannot create directory: " + code.message();
            return false;
        }
        if (text.size() > 32 * 1024 * 1024) {
            error = "Cannot save " + filename + " (maximum 32 MiB)";
            return false;
        }
        auto staging = path;
        staging += ".tmp";
        {
            std::ofstream file(staging, std::ios::binary | std::ios::trunc);
            if (!file) {
                error = "Cannot write " + filename;
                return false;
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();
            if (!file) {
                error = "Could not finish writing " + filename;
                return false;
            }
            file.close();
            if (file.fail()) {
                error = "Could not close " + filename;
                return false;
            }
        }
        // std::filesystem::rename cannot replace an existing file on Windows.
        if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = "Could not replace " + filename + " (Windows error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    }

    bool MapFiles::ReadPath(const std::string &filename, std::string &text, std::string &error) {
        std::error_code code;
        const auto size = std::filesystem::file_size(std::filesystem::u8path(filename), code);
        if (code || size > 32 * 1024 * 1024) {
            error = "Cannot read " + filename + " (maximum 32 MiB)";
            return false;
        }
        std::ifstream file(std::filesystem::u8path(filename), std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (!file || file.bad()) {
            error = "Could not read " + filename;
            return false;
        }
        text = buffer.str();
        return true;
    }

    std::string MapFiles::ChoosePath(bool save, bool blueprint, const std::string &current, std::string &error) {
        error.clear();
        wchar_t path[32768] {};
        // The filename field rejects forward slashes. Keep the directory separate
        // and normalize separators from our portable project paths.
        const auto initial   = std::filesystem::u8path(current).make_preferred();
        const auto directory = initial.parent_path().wstring();
        const auto filename  = initial.filename().wstring();
        if (filename.size() >= std::size(path)) {
            error = "The filename is too long.";
            return {};
        }
        std::wcsncpy(path, filename.c_str(), std::size(path) - 1);
        OPENFILENAMEW dialog {};
        dialog.lStructSize     = sizeof(dialog);
        dialog.hwndOwner       = DialogOwner();
        dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
        dialog.lpstrFile       = path;
        dialog.nMaxFile        = static_cast<DWORD>(std::size(path));
        dialog.lpstrFilter     = blueprint ? L"World Builder blueprint\0*.blueprint.json\0JSON files\0*.json\0\0" : L"World Builder project\0*.project.json\0JSON files\0*.json\0\0";
        dialog.lpstrDefExt     = blueprint ? L"blueprint.json" : L"project.json";
        dialog.Flags           = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
            if (const DWORD code = CommDlgExtendedError()) {
                error = "Could not open the file dialog (Windows error " + std::to_string(code) + ").";
            }
            return {};
        }
        const auto utf8 = std::filesystem::path(path).u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    bool MapFiles::ListDirectories(const std::string &path, std::string &directory, std::vector<std::string> &entries, std::string &error) {
        entries.clear();
        directory.clear();
        error.clear();
        try {
            const auto root = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
            for (const auto &entry : std::filesystem::directory_iterator(root)) {
                std::error_code code;
                if (entry.is_directory(code)) {
                    const auto utf8 = entry.path().filename().u8string();
                    entries.emplace_back(utf8.begin(), utf8.end());
                }
            }
            std::sort(entries.begin(), entries.end());
            const auto utf8 = root.u8string();
            directory.assign(utf8.begin(), utf8.end());
            return true;
        }
        catch (const std::filesystem::filesystem_error &) {
            entries.clear();
            error = "Could not open this folder. Check that it exists and you have access.";
            return false;
        }
    }

} // namespace KCDC::Features::Prop
