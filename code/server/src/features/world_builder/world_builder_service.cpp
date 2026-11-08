#include "world_builder_service.h"
#include "features/patrol/patrol_routes.h"

#include "core/server.h"
#include "features/ground_item/ground_item_verbs.h"
#include "features/level_edit/level_edit_verbs.h"
#include "features/prop/prop_verbs.h"
#include "features/smart_object/smart_object_service.h"
#include "features/vfx/vfx_verbs.h"
#include "shared/features/world_builder/world_document.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace KCDC::Features::WorldBuilder {
    namespace {
        constexpr const char *kLogger = "WorldBuilder";

        // Exports are capped where the client caps what it writes.
        constexpr std::uintmax_t kMaxResourceBytes = 32 * 1024 * 1024;

        glm::vec3 ReadVector(const nlohmann::json &value) {
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
        }

        // Exports store quaternions as [x, y, z, w]; glm takes w first.
        glm::quat ReadRotation(const nlohmann::json &value) {
            return {value.at(3).get<float>(), value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
        }

        // The filename without `.world.json`, or its plain stem for any other name.
        std::string NameFromPath(const std::string &filename) {
            const std::filesystem::path path = std::filesystem::u8path(filename);
            const std::u8string file         = path.filename().u8string();
            std::string name(file.begin(), file.end());
            constexpr std::string_view kSuffix = ".world.json";
            if (name.ends_with(kSuffix)) {
                name.resize(name.size() - kSuffix.size());
                return name;
            }
            const std::u8string stem = path.stem().u8string();
            return std::string(stem.begin(), stem.end());
        }
    } // namespace

    bool WorldBuilderService::Load(const nlohmann::json &entries, const std::string &level, std::string &error) {
        error.clear();
        if (_ready) {
            error = "World resources have already been loaded";
            return false;
        }
        if (!entries.is_array()) {
            error = "mod.world_resources must be an array of exported .world.json paths";
            return false;
        }

        std::vector<nlohmann::json> documents;
        std::vector<WorldResourceRecord> resources;
        std::vector<std::filesystem::path> seen;
        std::unordered_set<std::string> names;

        // Read and validate every file first, so a typo in the last one changes no world.
        for (const nlohmann::json &entry : entries) {
            const bool named = entry.is_object() && entry.contains("path") && entry["path"].is_string() && entry.contains("name") && entry["name"].is_string();
            if (!entry.is_string() && !named) {
                error = "mod.world_resources entries must be file paths or {name, path} objects";
                return false;
            }
            const std::string filename = (named ? entry["path"] : entry).get<std::string>();
            const std::string name     = named ? entry["name"].get<std::string>() : NameFromPath(filename);
            if (filename.empty() || name.empty()) {
                error = "World resource name and path must not be empty: " + filename;
                return false;
            }
            if (!names.insert(name).second) {
                error = "Duplicate world resource name '" + name + "': " + filename;
                return false;
            }

            std::error_code code;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::u8path(filename), code);
            if (code) {
                error = "Invalid world resource path: " + filename;
                return false;
            }
            // Canonical spelling alone misses hard links and Windows case aliases.
            for (const std::filesystem::path &previous : seen) {
                const bool same = std::filesystem::equivalent(previous, canonical, code);
                if (code) {
                    error = "Cannot compare world resource files: " + filename;
                    return false;
                }
                if (same) {
                    error = "Duplicate world resource file: " + filename;
                    return false;
                }
            }
            const std::uintmax_t size = std::filesystem::file_size(canonical, code);
            if (code || size > kMaxResourceBytes) {
                error = "Cannot read world resource (maximum 32 MiB): " + filename;
                return false;
            }
            seen.push_back(canonical);

            std::ifstream file(canonical, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            nlohmann::json document;
            if (!file || !Shared::WorldBuilder::ReadResource(text.str(), document, error)) {
                error = filename + ": " + (error.empty() ? std::string("could not be read") : error);
                return false;
            }
            if (document["level"] != level) {
                error = filename + ": exported for " + document["level"].get<std::string>() + ", server uses " + level;
                return false;
            }
            documents.push_back(std::move(document));

            const std::u8string path = canonical.u8string();
            WorldResourceRecord resource;
            resource.handle = _nextHandle++;
            resource.name   = name;
            resource.path.assign(path.begin(), path.end());
            resource.level = level;
            resources.push_back(std::move(resource));
        }

        for (std::size_t index = 0; index < documents.size(); ++index) {
            if (!Spawn(documents[index], resources[index], error)) {
                error = resources[index].path + ": " + error;
                DestroyContents(resources);
                return false;
            }
            const WorldResourceRecord &resource = resources[index];
            Framework::Logging::GetLogger(kLogger)->info("Loaded world resource '{}' from '{}' ({} props, {} effects, {} lights, {} level edits, {} doors, {} smart objects, {} items, {} areas, {} patrol routes)", resource.name, resource.path, resource.props.size(),
                resource.effects.size(), resource.lights.size(), resource.levelEdits.size(), resource.doors.size(), resource.smartObjects.size(), resource.items.size(), resource.areas.size(), resource.patrolRoutes.size());
        }
        _resources = std::move(resources);
        _ready     = true;
        return true;
    }

    bool WorldBuilderService::Spawn(const nlohmann::json &document, WorldResourceRecord &resource, std::string &error) {
        try {
            for (const auto &entry : document.value("patrolRoutes", nlohmann::json::array())) {
                Shared::Game::PatrolRouteDefinition route;
                if (!Shared::Game::PatrolRouteFromJson(entry, route, error)) {
                    return false;
                }
                resource.patrolRoutes.push_back(Patrol::PatrolRoutes::Get().Create(route));
            }
        }
        catch (const std::exception &exception) {
            error = exception.what();
            return false;
        }
        for (const nlohmann::json &entry : document["objects"]) {
            const std::string model               = entry["model"].get<std::string>();
            const std::size_t index               = Shared::Game::FindPropModelIndex(model);
            Shared::Entities::PropPhysics physics = Shared::Entities::PropPhysics::Static;
            if (!Shared::Entities::ParsePropPhysics(entry["physics"].get<std::string>(), physics)) {
                error = "Unknown prop physics for " + model;
                return false;
            }
            Shared::Entities::PropEntity *prop = Prop::Props::Spawn(index, index == Shared::Game::kNoPropModel ? model : std::string(), ReadVector(entry["position"]), ReadRotation(entry["rotation"]), 1.0F, physics, MafiaNet::VIRTUAL_WORLD_GLOBAL, ReadVector(entry["scale"]));
            if (prop == nullptr) {
                error = "Could not spawn prop (invalid model or removed by a spawn handler): " + model;
                return false;
            }
            resource.props.push_back(prop->GetNetworkID());
        }

        for (const nlohmann::json &entry : document["effects"]) {
            const std::string name                  = entry["effect"].get<std::string>();
            const Shared::Game::VfxEffectRef effect = Shared::Game::VfxEffectRef::Catalog(Shared::Game::FindVfxEffectIndex(name));
            Shared::Entities::VfxEntity *spawned    = Vfx::Effects::Spawn(effect, ReadVector(entry["position"]), ReadRotation(entry["rotation"]), Shared::WorldBuilder::ReadTuning(entry["tuning"]), MafiaNet::VIRTUAL_WORLD_GLOBAL);
            if (spawned == nullptr) {
                error = "Could not spawn effect (invalid effect or removed by a spawn handler): " + name;
                return false;
            }
            resource.effects.push_back(spawned->GetNetworkID());
        }

        for (const nlohmann::json &entry : document.value("lights", nlohmann::json::array())) {
            Shared::Game::LightTuning tuning;
            if (!Shared::WorldBuilder::ReadLightTuning(entry, tuning)) {
                error = "Invalid light tuning";
                return false;
            }
            auto *light = Server::Get()->GetLights().Spawn(ReadVector(entry["position"]), tuning);
            if (light == nullptr) {
                error = "Could not spawn light";
                return false;
            }
            resource.lights.push_back(light->GetNetworkID());
        }

        for (const nlohmann::json &entry : document["world"]) {
            Shared::Game::LevelEditDefinition definition;
            if (!Shared::Game::LevelEditDefinitionFromJson(entry, definition, error)) {
                return false;
            }
            if (LevelEdit::LevelEdits::Find(definition, MafiaNet::VIRTUAL_WORLD_GLOBAL) != nullptr) {
                error = "Two resources edit the same level object";
                return false;
            }
            Shared::Entities::LevelEditEntity *edit = LevelEdit::LevelEdits::Apply(definition, MafiaNet::VIRTUAL_WORLD_GLOBAL, error);
            if (edit == nullptr) {
                return false;
            }
            resource.levelEdits.push_back(edit->GetNetworkID());
        }

        for (const nlohmann::json &entry : document.value("doors", nlohmann::json::array())) {
            Shared::Game::DoorDefinition definition;
            if (!Shared::Game::DoorDefinitionFromJson(entry, definition, error)) {
                return false;
            }
            Shared::Entities::DoorEntity *door = Server::Get()->GetDoors().Spawn(definition, MafiaNet::VIRTUAL_WORLD_GLOBAL, error);
            if (door == nullptr) {
                return false;
            }
            resource.doors.push_back(door->GetNetworkID());
        }

        for (const nlohmann::json &entry : document.value("smartObjects", nlohmann::json::array())) {
            Shared::Game::SmartObjectDefinition definition;
            if (!Shared::Game::SmartObjectDefinitionFromJson(entry, definition, error)) {
                return false;
            }
            Shared::Entities::SmartObjectEntity *object = SmartObject::SmartObjectService::Get().Spawn(definition, MafiaNet::VIRTUAL_WORLD_GLOBAL, error);
            if (object == nullptr) {
                return false;
            }
            resource.smartObjects.push_back(object->GetNetworkID());
        }

        for (const nlohmann::json &entry : document.value("items", nlohmann::json::array())) {
            Shared::Game::ItemPlacementDefinition placement;
            if (!Shared::Game::ItemPlacementDefinitionFromJson(entry, placement, error)) {
                return false;
            }
            Shared::Entities::GroundItemEntity *item = GroundItem::GroundItems::Place(placement, MafiaNet::VIRTUAL_WORLD_GLOBAL, error);
            if (item == nullptr) {
                return false;
            }
            resource.items.push_back(item->GetNetworkID());
        }

        for (const nlohmann::json &entry : document["areas"]) {
            Shared::Game::AreaDefinition definition;
            if (!Shared::Game::AreaDefinitionFromJson(entry, definition, error)) {
                return false;
            }
            const auto *area = Server::Get()->GetAreas().Create(std::move(definition), error);
            if (area == nullptr) {
                return false;
            }
            resource.areas.push_back(area->handle);
        }
        return true;
    }

    void WorldBuilderService::Reset() {
        _ready                                           = false;
        const std::vector<WorldResourceRecord> resources = std::exchange(_resources, {});
        DestroyContents(resources);
    }

    void WorldBuilderService::DestroyContents(const std::vector<WorldResourceRecord> &resources) {
        auto *replication = Framework::CoreModules::GetReplication();
        for (const WorldResourceRecord &resource : resources) {
            for (const std::uint32_t handle : resource.patrolRoutes) {
                Patrol::PatrolRoutes::Get().Destroy(handle);
            }
            for (const std::uint32_t handle : resource.areas) {
                Server::Get()->GetAreas().Destroy(handle);
            }
            if (replication == nullptr) {
                continue;
            }
            for (const std::vector<std::uint64_t> *ids : {&resource.props, &resource.effects, &resource.lights, &resource.levelEdits, &resource.doors, &resource.smartObjects}) {
                for (const std::uint64_t id : *ids) {
                    if (auto *entity = replication->GetEntityByNetworkID(id)) {
                        replication->DestroyEntity(entity);
                    }
                }
            }
            // Through the verb, so the stack's bookkeeping and its destroy event go with it.
            for (const std::uint64_t id : resource.items) {
                if (auto *item = replication->GetEntity<Shared::Entities::GroundItemEntity>(id)) {
                    GroundItem::GroundItems::Destroy(item);
                }
            }
        }
    }

    const WorldResourceRecord *WorldBuilderService::Find(const std::string &name) const {
        const auto found = std::find_if(_resources.begin(), _resources.end(), [&name](const WorldResourceRecord &resource) {
            return resource.name == name;
        });
        return found != _resources.end() ? &*found : nullptr;
    }

    const WorldResourceRecord *WorldBuilderService::Get(std::uint32_t handle) const {
        const auto found = std::find_if(_resources.begin(), _resources.end(), [handle](const WorldResourceRecord &resource) {
            return resource.handle == handle;
        });
        return found != _resources.end() ? &*found : nullptr;
    }
} // namespace KCDC::Features::WorldBuilder
