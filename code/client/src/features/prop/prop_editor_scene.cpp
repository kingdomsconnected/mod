#include "prop_editor_scene.h"

#include "prop_editor_objects.h"
#include "prop_editor_support.h"

#include "common/module_support.h"
#include "common/vector_math.h"
#include "shared/game/item_classes.h"
#include "shared/game/smart_object_blueprints.h"

#include <logging/logger.h>

#include <algorithm>
#include <cctype>

#include "core/application.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_render_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/entitymodule/c_item_database.h"
#include "sdk/wh/entitymodule/s_player_item_class.h"

namespace KCDC::Features::Prop {
    namespace {
        KCDC::Common::Spawn::PropSpawner *Spawner() {
            auto *application = Core::gApplication.get();
            return application != nullptr ? &application->GetPropSpawner() : nullptr;
        }

        /**
         * A preview smart object's parts: beside the replicated ones' 0xF5AA,
         * so the game never takes one for the other, and fresh for every
         * build, since a removed entity's GUID stays taken until the entity
         * system collects it.
         */
        constexpr std::uint64_t kPreviewSmartObjectGuidTag = 0xF5AB000000000000ULL;
    } // namespace

    std::string ItemPreviewMesh(const std::string &itemClass) {
        const Shared::RPC::ItemClassId id = Shared::Game::ItemClassIdFromGuid(itemClass);
        auto *database                    = SDK::wh::entitymodule::C_ItemDatabase::GetInstance();
        const auto *entry                 = database != nullptr && id != Shared::RPC::ItemClassId {} ? database->FindItemClass({id.low, id.high}) : nullptr;
        if (entry == nullptr || !entry->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::PlayerItem)) {
            return {};
        }
        const char *model = static_cast<const SDK::wh::entitymodule::S_PickableItemClass *>(entry)->GetModel();
        if (model == nullptr || *model == '\0') {
            return {};
        }
        // The tables name most meshes from inside `objects/`, and without an
        // extension, which the item's own loader supplies: `manmade/food/food/fruits/apple`.
        std::string path(model);
        std::string lowered(path);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!lowered.starts_with("objects/") && !lowered.starts_with("objects\\")) {
            path = "objects/" + path;
        }
        const std::size_t leaf = path.find_last_of("/\\");
        if (path.find('.', leaf == std::string::npos ? 0 : leaf) == std::string::npos) {
            path += ".cgf";
        }
        return path;
    }

    std::uint64_t EditorScene::_previewSerial = 0;

    const PlacedObject *EditorScene::Find(std::uint64_t id) const {
        // Sorted by id: ids only grow, and Restore inserts in order.
        const auto found = std::lower_bound(_objects.begin(), _objects.end(), id, [](const PlacedObject &object, std::uint64_t wanted) {
            return object.id < wanted;
        });
        return found != _objects.end() && found->id == id ? &*found : nullptr;
    }

    std::uint64_t EditorScene::FindByEntityId(SDK::EntityId entityId) const {
        if (entityId == 0) {
            return 0;
        }

        const auto found = std::find_if(_objects.begin(), _objects.end(), [entityId](const PlacedObject &object) {
            return object.entityId == entityId;
        });
        if (found != _objects.end()) {
            return found->id;
        }
        // Any part of a smart object picks the whole.
        for (const auto &[id, assembly] : _assemblies) {
            if (assembly->OwnsEntity(entityId)) {
                return id;
            }
        }
        return 0;
    }

    std::uint64_t EditorScene::FindByPick(const WorldPick &pick) const {
        if (pick.kind == WorldPickKind::Entity) {
            return FindByEntityId(pick.entityId);
        }
        if (pick.kind != WorldPickKind::RenderNode || pick.node == nullptr) {
            return 0;
        }
        // Compared, never followed: a listed node may have streamed out since.
        const auto found = std::find_if(_objects.begin(), _objects.end(), [&pick](const PlacedObject &object) {
            return object.renderNode == pick.node;
        });
        return found != _objects.end() ? found->id : 0;
    }

    PlacedObject *EditorScene::FindMutable(std::uint64_t id) {
        return const_cast<PlacedObject *>(static_cast<const EditorScene *>(this)->Find(id));
    }

    bool EditorScene::BuildAssembly(PlacedObject &object) {
        const Shared::Game::SmartObjectBlueprint *blueprint = Shared::Game::FindSmartObjectBlueprint(object.model);
        if (blueprint == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The map editor has no blueprint '{}'", object.model);
            return false;
        }
        auto assembly                = std::make_unique<SmartObject::SmartObjectAssembly>();
        const std::uint64_t guidBase = kPreviewSmartObjectGuidTag | ((++_previewSerial & 0xFFFFFFFFFFULL) << 8);
        if (assembly->Build(*blueprint, guidBase, KCDC::Common::ToGlm(object.position), KCDC::Common::ToGlm(object.rotation)) != SmartObject::SmartObjectAssembly::BuildResult::Built || assembly->Parts().empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The map editor could not assemble '{}'", object.model);
            return false;
        }
        object.entityId               = assembly->Parts().front();
        object.collisionDirty         = false;
        object.collisionRebuildFailed = false;
        _assemblies[object.id]        = std::move(assembly);
        return true;
    }

    bool EditorScene::BuildEntity(PlacedObject &object) {
        if (object.kind == PlacedKind::SmartObject) {
            return BuildAssembly(object);
        }
        KCDC::Common::Spawn::PropSpawner *spawner = Spawner();
        if (spawner == nullptr) {
            return false;
        }

        // A door shows its leaf and an item its mesh, neither of them solid:
        // the export, not the preview, is what the server builds.
        KCDC::Common::Spawn::PropSpawnRequest request;
        request.model   = object.model;
        request.physics = object.physics;
        if (object.kind == PlacedKind::Item) {
            request.model = ItemPreviewMesh(object.model);
            if (request.model.empty()) {
                request.model = kWorldObjectStandInMesh;
            }
        }
        if (object.kind != PlacedKind::Prop) {
            request.physics = KCDC::Common::Spawn::PropPhysics::None;
        }

        // The proxy takes one scale; the largest axis keeps collision the right size.
        request.scale = std::max({object.scale.x, object.scale.y, object.scale.z, 0.01F});
        request.world = KCDC::Common::Spawn::WorldPlacement {object.position, object.rotation};

        KCDC::Common::Spawn::PropSpawnOutcome outcome = spawner->SpawnNow(request);
        if (outcome.prop.entityId == 0 && object.kind == PlacedKind::Item && request.model != kWorldObjectStandInMesh) {
            // A worn piece is a skinned character the spawner cannot stand up as a prop.
            request.model = kWorldObjectStandInMesh;
            outcome       = spawner->SpawnNow(request);
        }
        if (outcome.prop.entityId == 0) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The map editor could not place '{}': {}", object.model, KCDC::Common::Spawn::PropSpawnResultToString(outcome.result));
            return false;
        }

        object.collisionScale         = request.scale;
        object.collisionDirty         = false;
        object.collisionRebuildFailed = false;
        object.entityId               = outcome.prop.entityId;
        ApplyTransform(object);
        return true;
    }

    void EditorScene::DestroyEntity(PlacedObject &object) {
        if (const auto assembly = _assemblies.find(object.id); assembly != _assemblies.end() && object.kind == PlacedKind::SmartObject) {
            assembly->second->Destroy();
            _assemblies.erase(assembly);
            object.entityId = 0;
            return;
        }
        if (object.entityId == 0) {
            return;
        }
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            entitySystem->RemoveEntity(object.entityId, false);
        }
        object.entityId = 0;
    }

    void EditorScene::ApplyTransform(const PlacedObject &object) {
        if (object.kind == PlacedKind::SmartObject) {
            if (const auto assembly = _assemblies.find(object.id); assembly != _assemblies.end()) {
                assembly->second->Move(KCDC::Common::ToGlm(object.position), KCDC::Common::ToGlm(object.rotation));
            }
            return;
        }
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr && object.entityId != 0 ? entitySystem->GetEntity(object.entityId) : nullptr;
        if (entity == nullptr) {
            return;
        }

        // One call, so position and rotation never tear.
        entity->SetPosRotScale(object.position, object.rotation, object.scale);
    }

    std::uint64_t EditorScene::Place(const std::string &model, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale, KCDC::Common::Spawn::PropPhysics physics) {
        if (model.empty() || !KCDC::Common::IsFinite(position) || !KCDC::Common::IsFinite(scale)) {
            return 0;
        }

        PlacedObject object;
        object.id       = _nextId++;
        object.model    = model;
        object.name     = StemOf(model);
        object.position = position;
        object.rotation = rotation;
        object.scale    = ClampObjectScale(scale);
        object.physics  = physics;

        if (!BuildEntity(object)) {
            return 0;
        }

        _objects.push_back(std::move(object));
        return _objects.back().id;
    }

    std::uint64_t EditorScene::PlaceWorldObject(PlacedKind kind, const std::string &model, const nlohmann::json &settings, const SDK::Vec3 &position, const SDK::Quat &rotation, std::string &error) {
        if (kind == PlacedKind::Prop) {
            error = "a prop is placed by its mesh";
            return 0;
        }
        std::string canonical = model;
        nlohmann::json clean  = settings;
        if (!NormalizeWorldObject(kind, canonical, clean, error)) {
            return 0;
        }
        KCDC::Common::Spawn::WorldPlacement placement {position, rotation};
        if (!KCDC::Common::IsFinite(position) || !KCDC::Common::Spawn::NormalizeSpawnTransform(placement)) {
            error = "that is not a usable position and rotation";
            return 0;
        }

        PlacedObject object;
        object.id       = _nextId++;
        object.kind     = kind;
        object.model    = std::move(canonical);
        object.name     = WorldObjectName(kind, object.model);
        object.settings = std::move(clean);
        object.position = placement.position;
        object.rotation = placement.rotation;
        object.physics  = KCDC::Common::Spawn::PropPhysics::None;
        if (!BuildEntity(object)) {
            error = "the game would not build its preview; the log has the reason";
            return 0;
        }
        _objects.push_back(std::move(object));
        return _objects.back().id;
    }

    bool EditorScene::SetSettings(std::uint64_t id, const nlohmann::json &settings, std::string &error) {
        PlacedObject *object = FindMutable(id);
        if (object == nullptr || object->kind == PlacedKind::Prop) {
            error = object == nullptr ? "no such object" : "a prop has no settings";
            return false;
        }
        std::string model    = object->model;
        nlohmann::json clean = settings;
        if (!NormalizeWorldObject(object->kind, model, clean, error)) {
            return false;
        }
        object->settings = std::move(clean);
        return true;
    }

    bool EditorScene::SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale) {
        PlacedObject *object = FindMutable(id);
        if (object == nullptr || !KCDC::Common::IsFinite(position) || !KCDC::Common::IsFinite(scale)) {
            return false;
        }

        KCDC::Common::Spawn::WorldPlacement placement {position, rotation};
        if (!KCDC::Common::Spawn::NormalizeSpawnTransform(placement)) {
            return false;
        }

        // Doors, smart objects and items are built at their own size.
        const SDK::Vec3 clampedScale = object->kind == PlacedKind::Prop ? ClampObjectScale(scale) : SDK::Vec3 {1.0F, 1.0F, 1.0F};
        if (clampedScale.x != object->scale.x || clampedScale.y != object->scale.y || clampedScale.z != object->scale.z) {
            object->collisionDirty   = object->physics != KCDC::Common::Spawn::PropPhysics::None && std::max({clampedScale.x, clampedScale.y, clampedScale.z}) != object->collisionScale;
            object->collisionChanged = std::chrono::steady_clock::now();
        }
        object->position = placement.position;
        object->rotation = placement.rotation;
        object->scale    = clampedScale;
        ApplyTransform(*object);
        return true;
    }

    bool EditorScene::SetPhysics(std::uint64_t id, KCDC::Common::Spawn::PropPhysics physics) {
        PlacedObject *object = FindMutable(id);
        if (object == nullptr || object->physics == physics) {
            return object != nullptr;
        }
        // The server gives a door, smart object or item its own.
        if (object->kind != PlacedKind::Prop) {
            return false;
        }

        object->physics = physics;
        return Rebuild(id);
    }

    bool EditorScene::Rebuild(std::uint64_t id) {
        PlacedObject *object = FindMutable(id);
        if (object == nullptr) {
            return false;
        }
        // Its preview is keyed by the record, so the old one goes first.
        if (object->kind == PlacedKind::SmartObject) {
            DestroyEntity(*object);
            return BuildEntity(*object);
        }

        // Keep the live object if spawning its replacement fails.
        PlacedObject replacement = *object;
        replacement.entityId     = 0;
        object->collisionDirty   = false; // a failed rebuild is retried explicitly
        if (!BuildEntity(replacement)) {
            object->collisionRebuildFailed = true;
            return false;
        }
        DestroyEntity(*object);
        *object = std::move(replacement);
        return true;
    }

    std::uint64_t EditorScene::Duplicate(std::uint64_t id) {
        const PlacedObject *source = Find(id);
        if (source == nullptr) {
            return 0;
        }

        // Along the object's own X by its own width, so a turned fence panel continues.
        SDK::Vec3 localSize {1.0F, 1.0F, 1.0F};
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr && source->entityId != 0 ? entitySystem->GetEntity(source->entityId) : nullptr;
        if (entity != nullptr) {
            SDK::AABB local {};
            entity->GetLocalBounds(local);
            if (IsUsableBounds(local)) {
                localSize = SDK::Vec3 {local.max.x - local.min.x, local.max.y - local.min.y, local.max.z - local.min.z};
            }
        }

        if (source->kind == PlacedKind::SmartObject) {
            if (const auto assembly = _assemblies.find(id); assembly != _assemblies.end()) {
                SDK::AABB bounds {};
                if (assembly->second->Bounds(bounds) && IsUsableBounds(bounds)) {
                    localSize = SDK::Vec3 {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z};
                }
            }
        }

        const float step          = std::max(localSize.x * source->scale.x, 0.25F);
        const SDK::Matrix34 basis = SDK::Matrix34FromTransform(SDK::Vec3 {}, source->rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F});
        const SDK::Vec3 axis {basis.m[0][0], basis.m[1][0], basis.m[2][0]};
        const SDK::Vec3 position {source->position.x + axis.x * step, source->position.y + axis.y * step, source->position.z + axis.z * step};
        if (source->kind != PlacedKind::Prop) {
            std::string error;
            const PlacedObject copy = *source; // the vector may grow under `source`
            return PlaceWorldObject(copy.kind, copy.model, copy.settings, position, copy.rotation, error);
        }
        return Place(source->model, position, source->rotation, source->scale, source->physics);
    }

    bool EditorScene::Restore(const PlacedObject &record) {
        if (record.id == 0 || Find(record.id) != nullptr) {
            return false;
        }

        PlacedObject object = record;
        object.entityId     = 0;
        object.hasBounds    = false;
        if (!BuildEntity(object)) {
            return false;
        }

        const auto at = std::find_if(_objects.begin(), _objects.end(), [&object](const PlacedObject &other) {
            return other.id > object.id;
        });
        _objects.insert(at, std::move(object));
        _nextId = std::max(_nextId, record.id + 1);
        return true;
    }

    void EditorScene::SetGroup(std::uint64_t id, const std::string &group) {
        if (PlacedObject *record = FindMutable(id)) {
            record->group = group;
        }
    }

    bool EditorScene::Remove(std::uint64_t id) {
        const auto found = std::find_if(_objects.begin(), _objects.end(), [id](const PlacedObject &object) {
            return object.id == id;
        });
        if (found == _objects.end()) {
            return false;
        }

        DestroyEntity(*found);
        _objects.erase(found);
        return true;
    }

    void EditorScene::Clear() {
        for (PlacedObject &object : _objects) {
            DestroyEntity(object);
        }
        _objects.clear();
        _assemblies.clear();
    }

    void EditorScene::Forget() {
        for (auto &[id, assembly] : _assemblies) {
            assembly->Forget();
        }
        _assemblies.clear();
        _objects.clear();
    }

    void EditorScene::Refresh() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            for (PlacedObject &object : _objects) {
                object.entityId   = 0;
                object.renderNode = nullptr;
                object.hasBounds  = false;
            }
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        for (PlacedObject &object : _objects) {
            if (object.kind == PlacedKind::SmartObject) {
                const auto assembly = _assemblies.find(object.id);
                SDK::IEntity *part  = object.entityId != 0 ? entitySystem->GetEntity(object.entityId) : nullptr;
                SDK::AABB bounds {};
                object.renderNode = nullptr;
                object.hasBounds  = assembly != _assemblies.end() && part != nullptr && !part->IsGarbage() && assembly->second->Bounds(bounds) && IsUsableBounds(bounds);
                if (object.hasBounds) {
                    object.worldBounds = bounds;
                }
                else if (part == nullptr || part->IsGarbage()) {
                    object.entityId = 0;
                }
                continue;
            }
            if (object.collisionDirty && now - object.collisionChanged >= std::chrono::milliseconds(200)) {
                Rebuild(object.id);
            }
            SDK::IEntity *entity = object.entityId != 0 ? entitySystem->GetEntity(object.entityId) : nullptr;
            if (entity == nullptr || entity->IsGarbage()) {
                object.entityId   = 0;
                object.renderNode = nullptr;
                object.hasBounds  = false;
                continue;
            }

            // The octree hands picks back as this node rather than the entity.
            auto *render      = static_cast<SDK::IEntityRenderProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)));
            object.renderNode = render != nullptr ? render->GetRenderNode() : nullptr;

            SDK::AABB bounds {};
            entity->GetWorldBounds(bounds);
            object.hasBounds = IsUsableBounds(bounds);
            if (object.hasBounds) {
                object.worldBounds = bounds;
            }
        }
    }

    std::uint64_t EditorScene::PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers) const {
        std::uint64_t picked = 0;
        float nearest        = maxDistance;

        for (const PlacedObject &object : _objects) {
            if (layers && !layers->Editable("prop:" + std::to_string(object.id))) {
                continue;
            }
            if (!object.hasBounds) {
                continue;
            }

            const float hitDistance = SDK::RayBoxEntryDistance(origin, direction, object.worldBounds);
            if (hitDistance < 0.0F || hitDistance > nearest) {
                continue;
            }

            nearest = hitDistance;
            picked  = object.id;
        }

        distance = picked != 0 ? nearest : maxDistance;
        return picked;
    }

} // namespace KCDC::Features::Prop
