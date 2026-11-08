#include "world_query_service.h"

#include "core/application.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include "features/level_edit/level_edit_service.h"
#include "features/prop/prop_editor_objects.h"
#include "features/world/level_collider_module.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"

#include "sdk/cryengine/3dengine/i_material.h"
#include "sdk/cryengine/3dengine/i_material_manager.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/3dengine/i_surface_type.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_pose.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/entitysystem/s_entity_proximity_query.h"
#include "sdk/cryengine/physics/i_physical_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/profiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace KCDC::Features::World {
    namespace {
        constexpr const char *kLogger = "WorldQuery";

        /** What the engine's own profiler attributes our queries to. */
        constexpr const char *kRayNameTag = "KCDC";

        /** Every reported hit joins the skip list, alongside the local body's two colliders. */
        constexpr int kMaxSkippedColliders = static_cast<int>(KCDC::Shared::RPC::kMaxWorldQueryHits) + 2;

        SDK::IPhysicalEntity *CharacterPhysics(SDK::ICharacterInstance *character) {
            SDK::ISkeletonPose *pose = character != nullptr ? character->GetISkeletonPose() : nullptr;
            return pose != nullptr ? pose->GetCharacterPhysics() : nullptr;
        }

        /** The object types and ray flags a mode stands for, all from the game's own traces. */
        void ResolveRayMode(KCDC::Shared::RPC::WorldRayMode mode, int &objectTypes, unsigned int &flags) {
            switch (mode) {
            case KCDC::Shared::RPC::WorldRayMode::Anything:
                objectTypes = SDK::CPhysicalWorld::kAllObjectTypes;
                flags       = SDK::CPhysicalWorld::kPickRayFlags;
                return;
            case KCDC::Shared::RPC::WorldRayMode::Ground:
                objectTypes = SDK::CPhysicalWorld::kGroundObjectTypes;
                flags       = SDK::CPhysicalWorld::kGroundRayFlags;
                return;
            case KCDC::Shared::RPC::WorldRayMode::Cover: break;
            }
            objectTypes = SDK::CPhysicalWorld::kVisibilityObjectTypes;
            flags       = SDK::CPhysicalWorld::kVisibilityRayFlags;
        }

        /** Empty only when the material manager is not up: the engine answers every index. */
        std::string SurfaceName(std::int16_t surfaceIndex) {
            SDK::I3DEngine *engine        = SDK::I3DEngine::GetInstance();
            SDK::IMaterialManager *matman = engine != nullptr ? engine->GetMaterialManager() : nullptr;
            SDK::ISurfaceType *surface    = matman != nullptr ? matman->GetSurfaceType(surfaceIndex, kRayNameTag) : nullptr;
            const char *name              = surface != nullptr ? surface->GetName() : nullptr;
            return name != nullptr ? std::string(name).substr(0, std::min<std::size_t>(std::strlen(name), KCDC::Shared::RPC::kMaxWorldQueryTextLength)) : std::string {};
        }

        /** Bounded to what the wire and the sanity check accept. */
        std::string BoundedText(const char *text) {
            if (text == nullptr) {
                return {};
            }
            return std::string(text).substr(0, std::min<std::size_t>(std::strlen(text), KCDC::Shared::RPC::kMaxWorldQueryTextLength));
        }

        /** Mesh and material names are paths, bounded to what the wire accepts for one. */
        std::string BoundedPath(const char *text) {
            if (text == nullptr) {
                return {};
            }
            return std::string(text).substr(0, std::min<std::size_t>(std::strlen(text), KCDC::Shared::RPC::kMaxWorldQueryPathLength));
        }

        /**
         * The material drawn where the ray met it. A hit's original material
         * index is the triangle's own, before the physics mapped it to a
         * surface, which is the sub-material slot the mesh draws it with.
         */
        std::string MaterialName(SDK::IMaterial *material, std::int16_t subMaterial) {
            if (material == nullptr) {
                return {};
            }
            const int count       = material->GetSubMtlCount();
            SDK::IMaterial *drawn = count > 0 && subMaterial >= 0 && subMaterial < count ? material->GetSubMtl(subMaterial) : nullptr;
            return BoundedPath((drawn != nullptr ? drawn : material)->GetName());
        }

        /**
         * What a piece of static geometry is. A vegetation instance and a brush
         * that built its own physics hand the collider themselves as foreign
         * data (`CVegetation::CreatePhysics`, 0x1803FCDFC, creates its `PE_STATIC` with
         * `this, PHYS_FOREIGN_ID_STATIC`). A level brush's collider is built
         * by `C_PhysicalizationManager` with the same id and no node at all,
         * so only the pairing `LevelColliderModule` made at build time can
         * name its mesh.
         */
        void DescribeStatic(SDK::IPhysicalEntity *collider, std::int16_t subMaterial, KCDC::Features::World::WorldQueryService::RayHit &reported) {
            if (collider->GetiForeignData() != SDK::PHYS_FOREIGN_ID_STATIC) {
                return;
            }

            if (auto *node = static_cast<SDK::IRenderNode *>(collider->GetForeignData(SDK::PHYS_FOREIGN_ID_STATIC))) {
                switch (node->GetRenderNodeType()) {
                case SDK::eERType_Vegetation: reported.kind = KCDC::Shared::RPC::WorldHitKind::Vegetation; break;
                case SDK::eERType_Brush:
                case SDK::eERType_MovableBrush: reported.kind = KCDC::Shared::RPC::WorldHitKind::Brush; break;
                default: return;
                }
                reported.model    = BoundedPath(node->GetName());
                reported.material = MaterialName(node->GetMaterial(), subMaterial);
                return;
            }

            if (const auto placement = KCDC::Features::World::LevelColliderModule::Get().FindPlacement(collider)) {
                reported.kind     = KCDC::Shared::RPC::WorldHitKind::Brush;
                reported.model    = BoundedPath(placement->mesh->GetFilePath());
                reported.material = MaterialName(placement->mesh->GetMaterial(), subMaterial);
            }
        }

        /**
         * Names what was hit the way a level edit finds it again, when the
         * level placed it: where the level put it and how it turned and sized
         * it, read through the server's edits so an object one of them moved
         * still reports its key. A layer brush's collider has no node, so the
         * brush is found again from the mesh and the pivot its pairing kept.
         */
        void DescribeLevelObject(SDK::IPhysicalEntity *collider, SDK::IEntity *entity, KCDC::Features::World::WorldQueryService::RayHit &reported) {
            KCDC::Features::Prop::WorldPick pick;
            if (reported.kind == KCDC::Shared::RPC::WorldHitKind::Entity && entity != nullptr) {
                pick.kind     = KCDC::Features::Prop::WorldPickKind::Entity;
                pick.entityId = entity->GetId();
            }
            else if (reported.kind == KCDC::Shared::RPC::WorldHitKind::Vegetation || reported.kind == KCDC::Shared::RPC::WorldHitKind::Brush) {
                if (auto *node = static_cast<SDK::IRenderNode *>(collider->GetForeignData(SDK::PHYS_FOREIGN_ID_STATIC))) {
                    pick.node = node;
                }
                else if (const auto placement = KCDC::Features::World::LevelColliderModule::Get().FindPlacement(collider)) {
                    pick.node = KCDC::Features::Prop::WorldObjects::FindLevelBrushAt(reported.model, placement->position);
                }
                pick.kind = pick.node != nullptr ? KCDC::Features::Prop::WorldPickKind::RenderNode : KCDC::Features::Prop::WorldPickKind::None;
            }
            if (pick.kind == KCDC::Features::Prop::WorldPickKind::None || !KCDC::Core::gApplication) {
                return;
            }
            reported.levelObject = KCDC::Core::gApplication->GetLevelEditService().Objects().LevelPose(pick, reported.levelObjectAt, reported.levelObjectRotation, reported.levelObjectScale);
        }

        /** How both halves of a query report an entity. */
        void DescribeEntity(SDK::IEntity *entity, SDK::EntityId &entityId, SDK::EntityGUID &entityGuid, std::string &name, std::string &entityClass) {
            if (entity == nullptr) {
                return;
            }
            entityId                         = entity->GetId();
            entityGuid                       = entity->GetGuid();
            name                             = BoundedText(entity->GetName());
            SDK::IEntityClass *resolvedClass = entity->GetClass();
            entityClass                      = BoundedText(resolvedClass != nullptr ? resolvedClass->GetName() : nullptr);
        }

        /** Null when what was struck is terrain or static geometry. */
        SDK::IEntity *EntityBehind(SDK::IPhysicalEntity *collider) {
            if (collider == nullptr || collider->GetiForeignData() != SDK::PHYS_FOREIGN_ID_ENTITY) {
                return nullptr;
            }
            return static_cast<SDK::IEntity *>(collider->GetForeignData(SDK::PHYS_FOREIGN_ID_ENTITY));
        }

        float Length(const SDK::Vec3 &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }
    } // namespace

    KCDC::Shared::RPC::WorldRayHit WorldQueryService::ToWire(const RayHit &hit) {
        KCDC::Shared::RPC::WorldRayHit wire;
        wire.entityName  = hit.entityName;
        wire.entityClass = hit.entityClass;
        wire.surface     = hit.surface;
        wire.model       = hit.model;
        wire.material    = hit.material;
        wire.kind        = hit.kind;
        wire.entityGuid  = hit.entityGuid;
        wire.pointX      = hit.point.x;
        wire.pointY      = hit.point.y;
        wire.pointZ      = hit.point.z;
        wire.normalX     = hit.normal.x;
        wire.normalY     = hit.normal.y;
        wire.normalZ     = hit.normal.z;
        wire.distance    = hit.distance;
        wire.terrain     = hit.terrain;
        wire.levelObject = hit.levelObject;
        if (hit.levelObject) {
            wire.objectAt       = {hit.levelObjectAt.x, hit.levelObjectAt.y, hit.levelObjectAt.z};
            wire.objectRotation = {hit.levelObjectRotation.x, hit.levelObjectRotation.y, hit.levelObjectRotation.z, hit.levelObjectRotation.w};
            wire.objectScale    = {hit.levelObjectScale.x, hit.levelObjectScale.y, hit.levelObjectScale.z};
        }
        return wire;
    }

    WorldQueryService::RayHit WorldQueryService::DescribeContact(SDK::IPhysicalEntity *collider, const SDK::Vec3 &point, const SDK::Vec3 &normal, std::int16_t surfaceIndex, std::int16_t subMaterial, bool terrain) const {
        SDK::IEntity *entity = EntityBehind(collider);

        RayHit reported;
        reported.point   = point;
        reported.normal  = normal;
        reported.surface = SurfaceName(surfaceIndex);
        reported.terrain = terrain || collider == nullptr || collider->GetiForeignData() == SDK::PHYS_FOREIGN_ID_TERRAIN;
        DescribeEntity(entity, reported.entityId, reported.entityGuid, reported.entityName, reported.entityClass);
        if (reported.terrain) {
            reported.kind = KCDC::Shared::RPC::WorldHitKind::Terrain;
            return reported;
        }
        if (entity != nullptr) {
            reported.kind = KCDC::Shared::RPC::WorldHitKind::Entity;
        }
        else {
            DescribeStatic(collider, subMaterial, reported);
        }
        DescribeLevelObject(collider, entity, reported);
        return reported;
    }

    void WorldQueryService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Cannot register world query RPCs: network peer is unavailable during client initialization");
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::WorldQuery>([](const KCDC::Shared::RPC::WorldQuery &query, MafiaNet::Packet *) {
            if (query.IsSane()) {
                KCDC::Core::gApplication->GetWorldQueryService().QueueQuery(query);
            }
        });
    }

    void WorldQueryService::QueueQuery(const KCDC::Shared::RPC::WorldQuery &query) {
        std::lock_guard lock(_incomingMutex);
        if (_acceptIncoming.load()) {
            _incomingQueries.push_back(query);
        }
    }

    void WorldQueryService::Update() {
        _acceptIncoming.store(true);
        AnswerQueries();
    }

    void WorldQueryService::Reset() {
        _acceptIncoming.store(false);
        std::lock_guard lock(_incomingMutex);
        _incomingQueries.clear();
    }

    bool WorldQueryService::Raycast(const RayRequest &request, std::vector<RayHit> &hits) const {
        FW_PROFILE_SCOPE_N("KCDC World query raycast");

        auto *physics = SDK::CPhysicalWorld::GetInstance();
        if (physics == nullptr) {
            return false;
        }

        const SDK::Vec3 span {request.target.x - request.origin.x, request.target.y - request.origin.y, request.target.z - request.origin.z};

        int objectTypes       = 0;
        unsigned int rayFlags = 0;
        ResolveRayMode(request.mode, objectTypes, rayFlags);

        // Both of the body's colliders are named: the entity's and the
        // animated skeleton's are separate, and skipping only the first still
        // stops the ray on the second.
        SDK::IPhysicalEntity *skip[kMaxSkippedColliders] {};
        int skipCount = 0;
        if (request.ignoreLocalPlayer) {
            SDK::IEntity *localPlayer = KCDC::Common::LocalPlayer();
            SDK::IPhysicalEntity *candidates[] {localPlayer != nullptr ? localPlayer->GetPhysics() : nullptr, CharacterPhysics(localPlayer != nullptr ? localPlayer->GetCharacter(0) : nullptr)};
            for (SDK::IPhysicalEntity *collider : candidates) {
                if (collider != nullptr && std::find(skip, skip + skipCount, collider) == skip + skipCount) {
                    skip[skipCount++] = collider;
                }
            }
        }

        const int wanted = std::clamp(request.maxHits, 1, static_cast<int>(KCDC::Shared::RPC::kMaxWorldQueryHits));
        for (int found = 0; found < wanted; ++found) {
            SDK::SRayHit hit {};
            if (physics->RayWorldIntersection(request.origin, span, objectTypes, rayFlags, &hit, 1, skipCount != 0 ? skip : nullptr, skipCount) <= 0) {
                break;
            }

            auto *collider    = static_cast<SDK::IPhysicalEntity *>(hit.collider);
            RayHit reported   = DescribeContact(collider, hit.point, hit.normal, hit.surfaceIndex, hit.originalMaterialIndex, hit.isTerrain != 0);
            reported.distance = hit.distance;
            hits.push_back(std::move(reported));

            // A re-trace rather than CryPhysics' nMaxHits, which fills its
            // array tail with pierceable contacts instead of further solid
            // ones. Terrain has no collider, and nothing lies behind it.
            if (collider == nullptr || skipCount >= static_cast<int>(std::size(skip))) {
                break;
            }
            skip[skipCount++] = collider;
        }

        return true;
    }

    bool WorldQueryService::ResolveGround(const SDK::Vec3 &position, float probeUp, float probeDown, RayHit &ground) const {
        // A trace rather than a terrain elevation: a bridge, a floor or a
        // castle roof is geometry the heightmap knows nothing about.
        RayRequest request;
        request.origin            = SDK::Vec3 {position.x, position.y, position.z + probeUp};
        request.target            = SDK::Vec3 {position.x, position.y, position.z - probeDown};
        request.mode              = KCDC::Shared::RPC::WorldRayMode::Ground;
        request.maxHits           = 1;
        request.ignoreLocalPlayer = true;

        std::vector<RayHit> hits;
        if (!Raycast(request, hits) || hits.empty()) {
            return false;
        }

        ground = std::move(hits.front());
        return true;
    }

    bool WorldQueryService::EntitiesInRadius(const ProximityRequest &request, std::vector<NearbyEntity> &entities) const {
        FW_PROFILE_SCOPE_N("KCDC World query proximity");

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return false;
        }

        SDK::IEntityClass *filtered = nullptr;
        if (!request.entityClass.empty()) {
            SDK::IEntityClassRegistry *registry = entitySystem->GetClassRegistry();
            filtered                            = registry != nullptr ? registry->FindClass(request.entityClass.c_str()) : nullptr;
            // Matching nothing is the honest answer; widening to every class is not.
            if (filtered == nullptr) {
                return true;
            }
        }

        SDK::SEntityProximityQuery query;
        query.box.min     = SDK::Vec3 {request.centre.x - request.radius, request.centre.y - request.radius, request.centre.z - request.radius};
        query.box.max     = SDK::Vec3 {request.centre.x + request.radius, request.centre.y + request.radius, request.centre.z + request.radius};
        query.entityClass = filtered;
        entitySystem->QueryProximity(query);

        // The grid answers with a box; the corners are dropped here.
        const float radiusSquared = request.radius * request.radius;
        for (int index = 0; index < query.Count(); ++index) {
            SDK::IEntity *entity = query.At(index);
            if (entity == nullptr || entity->IsGarbage()) {
                continue;
            }

            SDK::IPhysicalEntity *physics = entity->GetPhysics();
            if (request.physicalOnly && physics == nullptr) {
                continue;
            }

            const SDK::Vec3 position = entity->GetWorldPos();
            const SDK::Vec3 separation {position.x - request.centre.x, position.y - request.centre.y, position.z - request.centre.z};
            const float distanceSquared = separation.x * separation.x + separation.y * separation.y + separation.z * separation.z;
            if (distanceSquared > radiusSquared) {
                continue;
            }

            NearbyEntity reported;
            reported.position     = position;
            reported.distance     = std::sqrt(distanceSquared);
            reported.physicalized = physics != nullptr;
            DescribeEntity(entity, reported.entityId, reported.entityGuid, reported.name, reported.entityClass);
            entities.push_back(std::move(reported));
        }

        std::sort(entities.begin(), entities.end(), [](const NearbyEntity &left, const NearbyEntity &right) {
            return left.distance < right.distance;
        });

        const std::size_t wanted = static_cast<std::size_t>(std::clamp(request.maxEntities, 1, static_cast<int>(KCDC::Shared::RPC::kMaxWorldQueryEntities)));
        if (entities.size() > wanted) {
            entities.resize(wanted);
        }
        return true;
    }

    void WorldQueryService::Answer(const KCDC::Shared::RPC::WorldQuery &query, KCDC::Shared::RPC::WorldQueryAnswer &answer) const {
        switch (query.kind) {
        case KCDC::Shared::RPC::WorldQueryKind::Raycast: {
            RayRequest request;
            request.origin  = SDK::Vec3 {query.originX, query.originY, query.originZ};
            request.target  = SDK::Vec3 {query.targetX, query.targetY, query.targetZ};
            request.mode    = query.rayMode;
            request.maxHits = static_cast<int>(query.maxResults);

            std::vector<RayHit> hits;
            if (!Raycast(request, hits)) {
                return;
            }
            for (const RayHit &hit : hits) {
                answer.hits.push_back(ToWire(hit));
            }
            answer.answered = true;
            return;
        }
        case KCDC::Shared::RPC::WorldQueryKind::Ground: {
            RayHit ground;
            // No ground is an ordinary answer, not a refusal.
            if (!ResolveGround(SDK::Vec3 {query.originX, query.originY, query.originZ}, query.probeUp, query.probeDown, ground)) {
                answer.answered = SDK::CPhysicalWorld::GetInstance() != nullptr;
                return;
            }

            answer.hits.push_back(ToWire(ground));
            answer.answered = true;
            return;
        }
        case KCDC::Shared::RPC::WorldQueryKind::Proximity: {
            ProximityRequest request;
            request.centre       = SDK::Vec3 {query.originX, query.originY, query.originZ};
            request.radius       = query.radius;
            request.entityClass  = query.entityClass;
            request.maxEntities  = static_cast<int>(query.maxResults);
            request.physicalOnly = query.physicalOnly;

            std::vector<NearbyEntity> nearby;
            if (!EntitiesInRadius(request, nearby)) {
                return;
            }
            for (const NearbyEntity &entity : nearby) {
                KCDC::Shared::RPC::WorldNearbyEntity wire;
                wire.name         = entity.name;
                wire.entityClass  = entity.entityClass;
                wire.entityGuid   = entity.entityGuid;
                wire.positionX    = entity.position.x;
                wire.positionY    = entity.position.y;
                wire.positionZ    = entity.position.z;
                wire.distance     = entity.distance;
                wire.physicalized = entity.physicalized;
                answer.entities.push_back(std::move(wire));
            }
            answer.answered = true;
            return;
        }
        }
    }

    void WorldQueryService::AnswerQueries() {
        std::vector<KCDC::Shared::RPC::WorldQuery> queries;
        {
            std::lock_guard lock(_incomingMutex);
            queries.swap(_incomingQueries);
        }

        auto *net = queries.empty() ? nullptr : Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        const std::string level       = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetCurrentLevelName() : std::string {};
        const std::uint32_t levelHash = level.empty() ? 0 : KCDC::Shared::RPC::LevelHash(level);
        for (const KCDC::Shared::RPC::WorldQuery &query : queries) {
            KCDC::Shared::RPC::WorldQueryAnswer answer;
            answer.requestId = query.requestId;
            answer.levelHash = levelHash;
            answer.kind      = query.kind;

            // Between levels there is nothing to stamp the answer with, so
            // the query is not run and the server hears that it was not.
            if (levelHash != 0) {
                Answer(query, answer);
            }

            net->BroadcastRPC(answer, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }
} // namespace KCDC::Features::World
