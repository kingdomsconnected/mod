#include "marker_service.h"

#include "marker.h"

#include "core/application.h"
#include "core/local_events.h"

#include "common/module_support.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/3dengine/i_material_manager.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_area_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/entitysystem/s_entity_event.h"

#include "shared/game/marker_catalog.h"

#include <algorithm>
#include <core_modules.h>
#include <cstdio>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Marker {
    namespace {
        constexpr const char *kLogger = "Marker";

        /** What the game's own Particle.SpawnEffect passes for "next free slot". */
        constexpr int kNextFreeSlot = -1;

        /** The `_noproxy` variants carry no physics proxy, so nothing has to be dephysicalized after loading one. */
        constexpr const char *kShapeGeometry[] = {
            nullptr, // Decal is not a mesh.
            "objects/special/primitive_cylinder_noproxy.cgf",
            "objects/special/primitive_sphere_noproxy.cgf",
            "objects/special/primitive_pyramid_noproxy.cgf",
            "objects/special/primitive_cube_noproxy.cgf",
            "objects/special/primitive_plane_noproxy.cgf",
        };
        static_assert(std::size(kShapeGeometry) == Shared::Entities::kMarkerShapeCount, "kShapeGeometry must carry one entry per MarkerShape");

        /**
         * Metres of mesh per metre asked for.
         *
         * These are CryEngine's editor primitives and are taken to be
         * unit-sized, which is the convention but is not proven in this build:
         * the shipped `.cgf` files are streaming stubs whose geometry lives in a
         * companion `.cgfm`, and nothing in the SDK reaches a loaded
         * `IStatObj`'s bounds. Correct it here if a 1 m marker is not 1 m.
         */
        constexpr float kPrimitiveUnitScale = 1.0F;

        /** A sphere around the marker's widest extent: the box and shape setters share the vtable run but neither has been read off its body. */
        float TriggerRadius(float size, float height) {
            return std::max(size, height) * 0.5F;
        }

        /**
         * The material a catalog row names, or null.
         *
         * Not cached: a material belongs to the level's material set, and a
         * level change is free to drop it. `LoadMaterial` is asked not to invent
         * a missing one, so an unresolvable name comes back null rather than as
         * an untextured placeholder.
         */
        SDK::IMaterial *ResolveMaterial(std::size_t materialIndex) {
            const Shared::Game::MarkerMaterialEntry *entry = Shared::Game::MarkerMaterialAt(materialIndex);
            SDK::I3DEngine *engine                         = SDK::I3DEngine::GetInstance();
            if (entry == nullptr || engine == nullptr) {
                return nullptr;
            }

            SDK::IMaterialManager *materials = engine->GetMaterialManager();
            if (materials == nullptr) {
                return nullptr;
            }

            // The catalog holds string_views over a compiled-in table, which is
            // not terminated at the view's end.
            char name[260];
            if (entry->name.size() >= sizeof(name)) {
                return nullptr;
            }
            std::snprintf(name, sizeof(name), "%.*s", static_cast<int>(entry->name.size()), entry->name.data());
            return materials->LoadMaterial(name, false, false, 0, true);
        }

        /**
         * A decal node is placed entirely by its matrix, and its scale sizes the
         * projection. Uniform because which axis is the projection depth has not
         * been read off a call site.
         */
        SDK::Matrix34 DecalMatrix(const glm::vec3 &position, const glm::quat &rotation, float size) {
            return SDK::Matrix34FromTransform(KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation)), SDK::Vec3 {size, size, size});
        }

        /** The slot-local transform: the asked-for extent, plus whatever the marker's own motion adds. */
        SDK::Matrix34 MeshSlotMatrix(float size, float height, float bobHeight = 0.0F, float spinDegrees = 0.0F) {
            const float wide     = size * kPrimitiveUnitScale;
            const float tall     = height * kPrimitiveUnitScale;
            const glm::quat spin = glm::angleAxis(glm::radians(spinDegrees), glm::vec3(0.0F, 0.0F, 1.0F));
            return SDK::Matrix34FromTransform(SDK::Vec3 {0.0F, 0.0F, bobHeight}, KCDC::Common::ToNative(spin), SDK::Vec3 {wide, wide, tall});
        }
    } // namespace

    SDK::IMaterial *MarkerService::TintedMaterial(std::size_t materialIndex, std::uint32_t color) {
        SDK::IMaterial *base = ResolveMaterial(materialIndex);
        if (base == nullptr || color == 0) {
            return base;
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(materialIndex) << 32) | color;
        if (auto it = _tinted.find(key); it != _tinted.end()) {
            return it->second;
        }

        SDK::IMaterialManager *materials = base->GetMaterialManager();
        SDK::IMaterial *clone            = materials != nullptr ? materials->CloneMaterial(base, nullptr) : nullptr;
        if (clone == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The material manager would not clone row {} for tinting", materialIndex);
            return base;
        }

        // The params live on the leaf, which for a plain material is the
        // material itself -- the game's own bind reads index 0 the same way.
        SDK::IMaterial *leaf = clone->GetSubMtl(0);
        if (leaf == nullptr) {
            leaf = clone;
        }

        SDK::Vec3 diffuse {static_cast<float>((color >> 24) & 0xFFU) / 255.0F, static_cast<float>((color >> 16) & 0xFFU) / 255.0F, static_cast<float>((color >> 8) & 0xFFU) / 255.0F};
        float opacity = static_cast<float>(color & 0xFFU) / 255.0F;

        // Named rather than refused: a shader that declares neither is a
        // material that cannot be tinted, which is worth one line and not a
        // failed marker.
        const bool tookDiffuse = leaf->SetMaterialParamVec3("diffuse", diffuse);
        const bool tookOpacity = leaf->SetMaterialParamFloat("opacity", opacity);
        if (!tookDiffuse && !tookOpacity) {
            Framework::Logging::GetLogger(kLogger)->warn("Material row {} declares neither diffuse nor opacity, so it cannot be tinted", materialIndex);
        }

        _tinted.emplace(key, clone);
        return clone;
    }

    void MarkerService::NativeContextChanged() {
        ++_nativeContext;
    }

    bool MarkerService::HasBudget() {
        if (_markers.size() < kMaxLiveMarkers) {
            return true;
        }
        if (!_warnedAtBudget) {
            _warnedAtBudget = true;
            Framework::Logging::GetLogger(kLogger)->warn("Refusing markers: {} are already placed, which is the budget", kMaxLiveMarkers);
        }
        return false;
    }

    std::uint32_t MarkerService::Remember(const Marker &built) {
        Marker marker = built;

        // Zero is the "no marker" answer, so it is skipped rather than issued.
        marker.handle = _nextHandle++;
        if (marker.handle == kNoHandle) {
            marker.handle = _nextHandle++;
        }
        _markers.emplace(marker.handle, marker);
        return marker.handle;
    }

    std::uint32_t MarkerService::PlaceDecal(std::uint64_t networkId, std::size_t materialIndex, const glm::vec3 &position, const glm::quat &rotation, float size, std::uint32_t color) {
        SDK::I3DEngine *engine   = SDK::I3DEngine::GetInstance();
        SDK::IMaterial *material = TintedMaterial(materialIndex, color);
        if (engine == nullptr || material == nullptr || !Shared::Game::MarkerMaterialSuits(materialIndex, true)) {
            return kNoHandle;
        }

        SDK::IRenderNode *node = engine->CreateRenderNode(SDK::eERType_Decal);
        if (node == nullptr) {
            return kNoHandle;
        }

        // The order the game's own level loader uses on a fresh decal node.
        node->SetMatrix(DecalMatrix(position, rotation, size));
        node->SetMaterial(material);
        engine->RegisterEntity(node);

        Marker marker;
        marker.networkId   = networkId;
        marker.node        = node;
        marker.appliedSize = size;
        return Remember(marker);
    }

    std::uint32_t MarkerService::PlaceMesh(std::uint64_t networkId, std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, std::uint32_t color) {
        const char *geometry     = kShapeGeometry[static_cast<std::uint8_t>(shape)];
        SDK::IMaterial *material = TintedMaterial(materialIndex, color);
        if (geometry == nullptr || material == nullptr) {
            return kNoHandle;
        }

        // The server refuses this combination, so reaching it means the peers
        // disagree about the catalog. Left on the primitive's own material: a
        // grey shape is a visibly wrong marker, where a decal material here
        // would silently wreck the lighting of everything behind it.
        if (!Shared::Game::MarkerMaterialSuits(materialIndex, false)) {
            Framework::Logging::GetLogger(kLogger)->warn("Marker material row {} is a decal and cannot dress a mesh; leaving the primitive's own material", materialIndex);
            material = nullptr;
        }

        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Marker_%u", _nextHandle);

        const SDK::EntityId entityId = KCDC::Common::SpawnCarrierEntity(SDK::EntityClassNames::AreaSphere, name, position, rotation);
        if (entityId == 0) {
            return kNoHandle;
        }

        SDK::IEntity *entity = KCDC::Common::ResolveEntity(entityId);
        const int slot       = entity != nullptr ? entity->LoadGeometry(kNextFreeSlot, geometry) : -1;
        if (entity == nullptr || slot < 0) {
            if (SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance()) {
                entities->RemoveEntity(entityId, false);
            }
            Framework::Logging::GetLogger(kLogger)->warn("The game would not load marker geometry \"{}\"", geometry);
            return kNoHandle;
        }

        if (material != nullptr) {
            entity->SetSlotMaterial(slot, material);
        }
        entity->SetSlotLocalTM(slot, MeshSlotMatrix(size, height));

        Marker marker;
        marker.networkId     = networkId;
        marker.entityId      = entityId;
        marker.slot          = slot;
        marker.appliedSize   = size;
        marker.appliedHeight = height;
        return Remember(marker);
    }

    std::uint32_t MarkerService::Place(std::uint64_t networkId, std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, std::uint32_t color, bool trigger) {
        const Shared::Game::MarkerMaterialEntry *entry = Shared::Game::MarkerMaterialAt(materialIndex);
        if (entry == nullptr || !HasBudget()) {
            return kNoHandle;
        }

        const std::uint32_t handle = Shared::Entities::IsMeshShape(shape) ? PlaceMesh(networkId, materialIndex, shape, position, rotation, size, height, color) : PlaceDecal(networkId, materialIndex, position, rotation, size, color);
        if (handle == kNoHandle) {
            return kNoHandle;
        }

        if (trigger) {
            if (Marker *marker = Find(handle)) {
                AttachTrigger(*marker, position, rotation, size, height);
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("Marker \"{}\" placed as {} handle {}{}", entry->name, Shared::Entities::MarkerShapeName(shape), handle, trigger ? " with an area" : "");
        return handle;
    }

    void MarkerService::AttachTrigger(Marker &marker, const glm::vec3 &position, const glm::quat &rotation, float size, float height) {
        SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
        if (entities == nullptr) {
            return;
        }

        // A projected marker is a render node and owns no entity, so the area
        // needs one spawned for it. A mesh marker already has its carrier.
        if (marker.entityId == 0) {
            char name[64];
            std::snprintf(name, sizeof(name), "KCDC_MarkerArea_%u", marker.handle);
            marker.entityId = KCDC::Common::SpawnCarrierEntity(SDK::EntityClassNames::AreaSphere, name, position, rotation);
        }

        SDK::IEntity *entity = KCDC::Common::ResolveEntity(marker.entityId);
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Marker handle {} has no entity to hang an area on", marker.handle);
            return;
        }

        // The entity keeps ownership of what CreateProxy builds; the share it
        // hands back is only a way to read the pointer out.
        auto proxy = entity->CreateProxy(static_cast<int>(SDK::EEntityProxy::Area));
        auto *area = static_cast<SDK::IEntityAreaProxy *>(proxy.get());
        if (area == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The entity system would not give marker handle {} an area proxy", marker.handle);
            return;
        }

        // Centred on the entity, which already stands at the marker's pose.
        area->SetSphere(SDK::Vec3 {0.0F, 0.0F, 0.0F}, TriggerRadius(size, height));

        entities->AddEntityEventListener(marker.entityId, SDK::ENTITY_EVENT_ENTERAREA, this);
        entities->AddEntityEventListener(marker.entityId, SDK::ENTITY_EVENT_LEAVEAREA, this);
        marker.listening = true;
    }

    void MarkerService::OnEntityEvent(SDK::IEntity *entity, SDK::SEntityEvent *event) {
        if (entity == nullptr || event == nullptr) {
            return;
        }

        const SDK::EEntityEvent id = event->Event();
        if (id != SDK::ENTITY_EVENT_ENTERAREA && id != SDK::ENTITY_EVENT_LEAVEAREA) {
            return;
        }

        // The area is ours; who crossed it is the event's first payload word.
        // Only the local body is reported: every other client watches its own,
        // and a remote body's crossing is that client's to send.
        const SDK::EntityId crossed = static_cast<SDK::EntityId>(event->Param(0));
        if (crossed == 0 || crossed != KCDC::Common::LocalActorEntityId()) {
            return;
        }

        // By entity rather than handle, and only on a crossing, so a walk is
        // the right shape here.
        const SDK::EntityId areaEntity = entity->GetId();
        for (const auto &[handle, marker] : _markers) {
            if (marker.entityId != areaEntity || marker.networkId == 0) {
                continue;
            }
            KCDC::Core::gApplication->GetLocalEvents().MarkerCrossed(marker.networkId, id == SDK::ENTITY_EVENT_ENTERAREA);
            return;
        }
    }

    bool MarkerService::Move(std::uint32_t handle, const glm::vec3 &position, const glm::quat &rotation, float size, float height) {
        Marker *marker = Find(handle);
        if (marker == nullptr) {
            return false;
        }

        // Both halves are stated, because a projected marker that is also a
        // trigger has a node to draw and a carrier whose area has to follow it.
        if (marker->node != nullptr) {
            marker->node->SetMatrix(DecalMatrix(position, rotation, size));
        }

        if (marker->entityId != 0) {
            SDK::IEntity *entity = KCDC::Common::ResolveEntity(marker->entityId);
            if (entity == nullptr) {
                return false;
            }
            KCDC::Common::PlaceEntity(entity, position, rotation);

            // Only when it actually changed: this runs every frame for a marker
            // that spins, and a slot write costs the engine a re-registration.
            if (marker->slot >= 0 && (size != marker->appliedSize || height != marker->appliedHeight)) {
                entity->SetSlotLocalTM(marker->slot, MeshSlotMatrix(size, height));
            }
        }

        marker->appliedSize   = size;
        marker->appliedHeight = height;
        return true;
    }

    bool MarkerService::Animate(std::uint32_t handle, float bobHeight, float spinDegrees, float size, float height) {
        Marker *marker = Find(handle);
        if (marker == nullptr || marker->slot < 0) {
            return false;
        }

        SDK::IEntity *entity = KCDC::Common::ResolveEntity(marker->entityId);
        if (entity == nullptr) {
            return false;
        }

        entity->SetSlotLocalTM(marker->slot, MeshSlotMatrix(size, height, bobHeight, spinDegrees));
        marker->appliedSize   = size;
        marker->appliedHeight = height;
        return true;
    }

    MarkerService::Marker *MarkerService::Find(std::uint32_t handle) {
        return const_cast<Marker *>(static_cast<const MarkerService *>(this)->Find(handle));
    }

    const MarkerService::Marker *MarkerService::Find(std::uint32_t handle) const {
        if (handle == kNoHandle) {
            return nullptr;
        }
        const auto it = _markers.find(handle);
        return it != _markers.end() ? &it->second : nullptr;
    }

    bool MarkerService::IsAlive(std::uint32_t handle) const {
        return Find(handle) != nullptr;
    }

    void MarkerService::Release(const Marker &marker) const {
        SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();

        if (marker.listening && entities != nullptr) {
            // The entity system compares a listener by pointer and never writes
            // through it, so handing it this from a const method is safe.
            auto *self = const_cast<MarkerService *>(this);
            entities->RemoveEntityEventListener(marker.entityId, SDK::ENTITY_EVENT_ENTERAREA, self);
            entities->RemoveEntityEventListener(marker.entityId, SDK::ENTITY_EVENT_LEAVEAREA, self);
        }

        if (marker.node != nullptr) {
            if (SDK::I3DEngine *engine = SDK::I3DEngine::GetInstance()) {
                engine->DeleteRenderNode(marker.node);
            }
        }

        // The whole carrier, not just the slot: this service spawned it and
        // nothing else hangs off it.
        if (marker.entityId != 0 && entities != nullptr) {
            entities->RemoveEntity(marker.entityId, false);
        }
    }

    bool MarkerService::Remove(std::uint32_t handle) {
        if (handle == kNoHandle) {
            return false;
        }

        const auto it = _markers.find(handle);
        if (it == _markers.end()) {
            return false;
        }
        Release(it->second);
        _markers.erase(it);
        return true;
    }

    void MarkerService::QueueRemove(std::uint32_t handle) {
        if (handle == kNoHandle) {
            return;
        }

        std::scoped_lock lock(_pendingRemovalsMutex);
        _pendingRemovals.push_back(handle);
    }

    void MarkerService::RemoveAll() {
        std::unordered_map<std::uint32_t, Marker> owned;
        owned.swap(_markers);
        for (const auto &[handle, marker] : owned) {
            Release(marker);
        }
    }

    void MarkerService::DrainRemovals() {
        std::vector<std::uint32_t> handles;
        {
            std::scoped_lock lock(_pendingRemovalsMutex);
            handles.swap(_pendingRemovals);
        }
        for (std::uint32_t handle : handles) {
            Remove(handle);
        }
    }

    void MarkerService::ObserveNativeContext() {
        const std::uint64_t current = _nativeContext.load();
        if (current == _observedContext) {
            return;
        }
        _observedContext = current;

        // Dropped rather than released: the world these render nodes and entity
        // ids named is gone, and the ids have already been handed out again --
        // which is also why the area listeners are not unregistered here, since
        // the entity system they were registered against went with the world.
        _markers.clear();
        _tinted.clear();
        {
            std::scoped_lock lock(_pendingRemovalsMutex);
            _pendingRemovals.clear();
        }
        _warnedAtBudget = false;

        // The replicas are still here and still want their markers.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::Marker::Marker>([](KCDC::Features::Marker::Marker *marker) {
                marker->Unbind();
            });
        }
    }

    void MarkerService::Update() {
        ObserveNativeContext();
        DrainRemovals();

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        replication->ForEach<KCDC::Features::Marker::Marker>([](KCDC::Features::Marker::Marker *marker) {
            marker->Frame();
        });
    }

    void MarkerService::Reset() {
        {
            std::scoped_lock lock(_pendingRemovalsMutex);
            _pendingRemovals.clear();
        }
        RemoveAll();

        // The clones belong to the level's material set, not to the session.
        _tinted.clear();
        _warnedAtBudget = false;
    }
} // namespace KCDC::Features::Marker
