#include "prop_editor_objects.h"

#include "prop_editor_support.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

#include "core/application.h"
#include "features/world/level_collider_module.h"

#include "shared/game/prop_models.h"
#include "shared/streamed_assets/streamed_asset_policy.h"

#include "sdk/cryengine/3dengine/c_brush.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_iterator.h"
#include "sdk/cryengine/entitysystem/i_entity_render_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/i_physical_entity.h"

namespace KCDC::Features::Prop {
    namespace {
        const char *RenderNodeTypeName(SDK::EERType type) {
            switch (type) {
            case SDK::eERType_Brush: return "Brush";
            case SDK::eERType_MovableBrush: return "Movable brush";
            case SDK::eERType_Vegetation: return "Vegetation";
            case SDK::eERType_GeomCache: return "Geometry cache";
            case SDK::eERType_CharacterDoNotUse: return "Character";
            case SDK::eERType_Light: return "Light";
            case SDK::eERType_ParticleEmitter: return "Particle emitter";
            case SDK::eERType_FogVolume: return "Fog volume";
            case SDK::eERType_Decal: return "Decal";
            case SDK::eERType_WaterVolume: return "Water volume";
            case SDK::eERType_WaterWave: return "Water wave";
            case SDK::eERType_Road: return "Road";
            case SDK::eERType_DistanceCloud: return "Distance cloud";
            case SDK::eERType_Rope: return "Rope";
            case SDK::eERType_TerrainSector: return "Terrain sector";
            case SDK::eERType_GameEffect: return "Game effect";
            case SDK::eERType_BreakableGlass: return "Breakable glass";
            case SDK::eERType_CloudBlocker: return "Cloud blocker";
            case SDK::eERType_MergedMesh:
            case SDK::eERType_MergedMeshInstance: return "Merged mesh";
            default: return "Render node";
            }
        }

        /** Node kinds worth editing. A whitelist, so a decal or light never takes a click meant for the wall behind it. */
        bool IsSelectableNodeType(SDK::EERType type) {
            switch (type) {
            case SDK::eERType_Brush:
            case SDK::eERType_MovableBrush:
            case SDK::eERType_Vegetation:
            case SDK::eERType_GeomCache:
            case SDK::eERType_CharacterDoNotUse: return true;
            default: return false;
            }
        }

        /** Classes never offered, whatever they carry; the geometry test does the rest. */
        constexpr const char *kUneditableEntityClasses[] {
            SDK::EntityClassNames::AudioListener,
            SDK::EntityClassNames::InteractionTrigger,
            SDK::EntityClassNames::SequenceTrigger,
            SDK::EntityClassNames::SmartObjectTrigger,
            SDK::EntityClassNames::ActionTrigger,
            SDK::EntityClassNames::KettleActionTrigger,
            SDK::EntityClassNames::FoodProcessingTrigger,
            SDK::EntityClassNames::WaterTubeActionTrigger,
            SDK::EntityClassNames::BedTrigger,
            SDK::EntityClassNames::TagPointWithScript,
            SDK::EntityClassNames::AreaUnion,
            SDK::EntityClassNames::RandomEventPlace,
            SDK::EntityClassNames::CatWaypoint,
            SDK::EntityClassNames::Rain,
        };

        /** Bit 31 asks for the whole compound rather than one sub-object. */
        constexpr int kCompoundStatObjSlot = static_cast<int>(0x80000000U);

        /** An entity worth editing has something to see: a mesh or a character. */
        bool IsSelectableEntity(SDK::IEntity *entity) {
            if (entity == nullptr || entity->IsGarbage() || entity->IsHidden()) {
                return false;
            }

            // The octree also sees the player's render node, even when physics
            // excludes their collider. It must not steal clicks on placed props.
            if (entity == KCDC::Common::LocalPlayer()) {
                return false;
            }

            SDK::IEntityClass *entityClass = entity->GetClass();
            const char *className          = entityClass != nullptr ? entityClass->GetName() : nullptr;
            if (className != nullptr) {
                for (const char *excluded : kUneditableEntityClasses) {
                    if (std::strcmp(className, excluded) == 0) {
                        return false;
                    }
                }
            }

            return entity->GetStatObj(kCompoundStatObjSlot) != nullptr || entity->GetStatObj(0) != nullptr || entity->GetCharacter(0) != nullptr;
        }

        // --- The octree pass -------------------------------------------------

        /** Octree reach, marched in slices: the box around a diagonal ray is as wide as it is long. */
        constexpr float kOctreeReach   = 250.0F;
        constexpr float kOctreeSegment = 32.0F;

        /** A Quick pick's reach with nothing solid ahead, where every slice costs a walk. */
        constexpr float kQuickOpenReach = 60.0F;

        /** Widens each slice so a node straddling two of them is in both. */
        constexpr float kOctreePadding = 1.5F;

        /** A query returning more than this is refused whole, never capped. */
        constexpr std::size_t kMaxOctreeNodes = 16384;

        /** Slack over the reported count, against drift between the two calls. */
        constexpr std::size_t kOctreeHeadroom = 64;

        /** Unofferable colliders (triggers, proxies) the ray may pass through. */
        constexpr int kMaxPierced = 6;

        /** Two brushes of one mesh this close are the same brush: the key every map and server edit is matched by. */
        constexpr float kBrushKeySlack = Shared::Game::kLevelEditKeySlack;

        /**
         * Brush edits re-found per tick. Each costs two octree queries, so a
         * long removal list is walked a slice at a time; a brush a layer
         * switch shows again stays visible until its slice comes round.
         */
        constexpr std::size_t kBrushResolvesPerTick = 48;

        /** Slack past a solid hit, so decoration lying flush still counts. */
        constexpr float kInFrontSlack = 0.05F;

        /** Largest box counted as decoration in front of a solid hit. */
        constexpr float kDecorationSpan = 10.0F;

        /** Above this, a box around the camera is a room, not an object. */
        constexpr float kEnclosingSpan = 4.0F;

        /** How far off the ray an entity origin may be; an origin is not a centre. */
        constexpr float kEntityStrayRadius = 20.0F;

        float Dot(const SDK::Vec3 &a, const SDK::Vec3 &b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        float BoxLongestSpan(const SDK::AABB &bounds) {
            return std::max({bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z});
        }

        /** Boxes this close in depth tie, and the smaller wins: the lantern, not the cathedral. */
        constexpr float kNearTieMetres = 1.0F;

        float BoxVolume(const SDK::AABB &bounds) {
            const float width  = std::max(bounds.max.x - bounds.min.x, 0.0F);
            const float depth  = std::max(bounds.max.y - bounds.min.y, 0.0F);
            const float height = std::max(bounds.max.z - bounds.min.z, 0.0F);
            return width * depth * height;
        }

        bool BoxContains(const SDK::AABB &bounds, const SDK::Vec3 &point) {
            return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y && point.y <= bounds.max.y && point.z >= bounds.min.z && point.z <= bounds.max.z;
        }

        /** A node owned by an entity is picked as that entity. */
        WorldPick DescribeNode(SDK::IRenderNode *node, const SDK::Vec3 &origin, const SDK::Vec3 &direction, float distance) {
            WorldPick pick;
            pick.distance = distance;
            pick.hitPoint = SDK::Vec3 {origin.x + direction.x * distance, origin.y + direction.y * distance, origin.z + direction.z * distance};

            SDK::IEntity *owner = node->GetOwnerEntity();
            if (owner != nullptr && !owner->IsGarbage()) {
                pick.kind      = WorldPickKind::Entity;
                pick.entityId  = owner->GetId();
                pick.foreignId = SDK::PHYS_FOREIGN_ID_ENTITY;
                return pick;
            }

            pick.kind      = WorldPickKind::RenderNode;
            pick.node      = node;
            pick.foreignId = SDK::PHYS_FOREIGN_ID_STATIC;
            return pick;
        }

        /** An entity's node is judged as the entity; a lone node by its kind. */
        bool IsPickableNode(SDK::IRenderNode *node) {
            if (node == nullptr || node->IsHidden()) {
                return false;
            }

            // Previews, props and swaps are bare brushes too.
            if (KCDC::Core::gApplication && KCDC::Core::gApplication->GetPropGhostService().OwnsNode(node)) {
                return false;
            }
            if (KCDC::Common::Spawn::StaticBrushRegistry::Get().Owns(node)) {
                return false;
            }

            if (SDK::IEntity *owner = node->GetOwnerEntity()) {
                return IsSelectableEntity(owner);
            }

            return IsSelectableNodeType(node->GetRenderNodeType());
        }
    } // namespace

    const char *WorldPickKindToString(WorldPickKind kind) {
        switch (kind) {
        case WorldPickKind::None: return "nothing";
        case WorldPickKind::RenderNode: return "level geometry";
        case WorldPickKind::Entity: return "entity";
        case WorldPickKind::Terrain: return "terrain";
        case WorldPickKind::Unknown: return "something the editor cannot hold";
        }
        return "?";
    }

    void DescribeWorldPick(const WorldPick &pick, std::string &label, std::string &detail) {
        label.clear();
        detail.clear();

        if (pick.kind == WorldPickKind::RenderNode && pick.node != nullptr) {
            // A brush's name is its .cgf path.
            const char *name       = pick.node->GetName();
            const char *typeName   = RenderNodeTypeName(pick.node->GetRenderNodeType());
            const std::string stem = StemOf(name != nullptr ? name : "");

            label  = stem.empty() ? typeName : stem;
            detail = name != nullptr && name[0] != '\0' ? std::string {typeName} + "  " + name : typeName;
            return;
        }

        if (pick.kind == WorldPickKind::Entity) {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr;
            if (entity == nullptr) {
                label = "(gone)";
                return;
            }

            const char *name      = entity->GetName();
            SDK::IEntityClass *cl = entity->GetClass();
            const char *className = cl != nullptr && cl->GetName() != nullptr ? cl->GetName() : "?";

            label  = name != nullptr && name[0] != '\0' ? name : "(unnamed)";
            detail = std::string {"Entity  "} + className;
            return;
        }

        label = WorldPickKindToString(pick.kind);
    }

    namespace {
        /** The octree applies no render-flag filter, so hidden nodes come back too. Zero past the guard, which sets `overflowed`. */
        std::size_t QueryLevelOctree(const SDK::AABB &box, std::vector<SDK::IRenderNode *> &nodes, bool *overflowed = nullptr) {
            auto *engine = SDK::I3DEngine::GetInstance();
            if (engine == nullptr) {
                nodes.clear();
                return 0;
            }

            const std::uint32_t available = engine->GetObjectsInBox(box, nullptr);
            if (overflowed != nullptr) {
                *overflowed = available > kMaxOctreeNodes;
            }

            // GetObjectsInBox takes no size and memcpys its whole list, so the
            // buffer must hold the count it just reported.
            if (available == 0 || available > kMaxOctreeNodes) {
                nodes.clear();
                return 0;
            }

            nodes.assign(static_cast<std::size_t>(available) + kOctreeHeadroom, nullptr);
            const std::uint32_t returned = engine->GetObjectsInBox(box, nodes.data());
            const std::size_t count      = std::min<std::size_t>(returned, nodes.size());
            nodes.resize(count);
            return count;
        }

        /** The brush with this mesh at `position`, within 5 cm: a brush's only key across runs. */
        SDK::IRenderNode *FindLevelBrush(const std::string &path, const SDK::Vec3 &position, std::vector<SDK::IRenderNode *> &scratch, const SDK::Vec3 *nearPoint = nullptr) {
            constexpr float kQuerySlack = 1.0F;
            constexpr float kMatchSlack = kBrushKeySlack;

            if (path.empty()) {
                return nullptr;
            }

            // A brush pivot can lie outside its bounds. A physics pick knows
            // a point on the surface and searches there instead.
            const auto &queryPosition = nearPoint != nullptr ? *nearPoint : position;
            SDK::AABB box {};
            box.min = SDK::Vec3 {queryPosition.x - kQuerySlack, queryPosition.y - kQuerySlack, queryPosition.z - kQuerySlack};
            box.max = SDK::Vec3 {queryPosition.x + kQuerySlack, queryPosition.y + kQuerySlack, queryPosition.z + kQuerySlack};

            SDK::IRenderNode *best = nullptr;
            float bestDistance     = kMatchSlack;

            const std::size_t count = QueryLevelOctree(box, scratch);
            for (std::size_t index = 0; index < count; ++index) {
                SDK::IRenderNode *node = scratch[index];
                if (node == nullptr || node->GetOwnerEntity() != nullptr) {
                    continue;
                }
                const char *name = node->GetName();
                if (name == nullptr || _stricmp(name, path.c_str()) != 0) {
                    continue;
                }
                // A prop or a swap the mod stands is no level brush, whatever mesh it shows.
                if (KCDC::Common::Spawn::StaticBrushRegistry::Get().Owns(node)) {
                    continue;
                }

                const SDK::Matrix34 matrix = node->GetMatrix();
                const SDK::Vec3 offset {matrix.m[0][3] - position.x, matrix.m[1][3] - position.y, matrix.m[2][3] - position.z};
                const float distance = std::sqrt(Dot(offset, offset));
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    best         = node;
                }
            }
            return best;
        }

        /** A level entity as the selection tools weigh it. */
        LevelCandidate EntityCandidate(SDK::IEntity *entity) {
            LevelCandidate candidate;
            candidate.pick.kind     = WorldPickKind::Entity;
            candidate.pick.entityId = entity->GetId();
            candidate.kind          = SelectableKinds::Entity;
            candidate.position      = entity->GetWorldPos();
            candidate.hidden        = entity->IsHidden();
            entity->GetWorldBounds(candidate.bounds);
            candidate.hasBounds = IsUsableBounds(candidate.bounds);
            const char *name    = entity->GetName();
            candidate.name      = name != nullptr ? name : "";
            if (SDK::IEntityClass *entityClass = entity->GetClass(); entityClass != nullptr && entityClass->GetName() != nullptr) {
                candidate.className = entityClass->GetName();
            }
            SDK::IStatObj *mesh = entity->GetStatObj(kCompoundStatObjSlot);
            if (mesh == nullptr) {
                mesh = entity->GetStatObj(0);
            }
            if (const char *path = mesh != nullptr ? mesh->GetFilePath() : nullptr) {
                candidate.mesh = path;
            }
            return candidate;
        }

        /** A bare level brush or plant, standing at `position`, as the selection tools weigh it. */
        LevelCandidate NodeCandidate(SDK::IRenderNode *node, const SDK::Vec3 &position) {
            LevelCandidate candidate;
            candidate.pick.kind = WorldPickKind::RenderNode;
            candidate.pick.node = node;
            candidate.kind      = node->GetRenderNodeType() == SDK::eERType_Vegetation ? SelectableKinds::Vegetation : SelectableKinds::Brush;
            candidate.position  = position;
            candidate.hidden    = node->IsHidden();
            node->FillBBox(candidate.bounds);
            candidate.hasBounds = IsUsableBounds(candidate.bounds);
            const char *name    = node->GetName();
            candidate.mesh      = name != nullptr ? name : "";
            candidate.name      = StemOf(candidate.mesh);
            return candidate;
        }

        /** Whether Collect lists this node: a bare node of a selectable type that is not one of the editor's ghosts. */
        bool IsCollectableNode(SDK::IRenderNode *node) {
            if (node == nullptr || node->GetOwnerEntity() != nullptr || !IsSelectableNodeType(node->GetRenderNodeType()) || KCDC::Common::Spawn::StaticBrushRegistry::Get().Owns(node)) {
                return false;
            }
            return !(KCDC::Core::gApplication && KCDC::Core::gApplication->GetPropGhostService().OwnsNode(node));
        }

        /** A node's pivot, read from its matrix. */
        SDK::Vec3 NodePosition(SDK::IRenderNode *node) {
            const SDK::Matrix34 matrix = node->GetMatrix();
            return SDK::Vec3 {matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
        }
    } // namespace

    std::size_t WorldObjects::QueryOctree(const SDK::AABB &box) const {
        return QueryLevelOctree(box, _octreeScratch);
    }

    WorldPick WorldObjects::PickPlant(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance) const {
        return PickFromOctree(origin, direction, maxDistance, nullptr, std::numeric_limits<float>::max(), true);
    }

    WorldPick WorldObjects::PickFromOctree(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, const SDK::Vec3 *avoid, float maxSpan, bool plantsOnly) const {
        WorldPick pick;

        if (SDK::I3DEngine::GetInstance() == nullptr) {
            return pick;
        }

        const float reach                      = std::min(maxDistance, kOctreeReach);
        std::vector<SDK::IRenderNode *> &nodes = _octreeScratch;

        // Near to far: the first slice with a hit holds the nearest.
        for (float begin = 0.0F; begin < reach; begin += kOctreeSegment) {
            const float finish = std::min(begin + kOctreeSegment, reach);

            const SDK::Vec3 head {origin.x + direction.x * begin, origin.y + direction.y * begin, origin.z + direction.z * begin};
            const SDK::Vec3 tail {origin.x + direction.x * finish, origin.y + direction.y * finish, origin.z + direction.z * finish};

            SDK::AABB query {};
            query.min = SDK::Vec3 {std::min(head.x, tail.x) - kOctreePadding, std::min(head.y, tail.y) - kOctreePadding, std::min(head.z, tail.z) - kOctreePadding};
            query.max = SDK::Vec3 {std::max(head.x, tail.x) + kOctreePadding, std::max(head.y, tail.y) + kOctreePadding, std::max(head.z, tail.z) + kOctreePadding};

            const std::size_t count = QueryOctree(query);
            if (count == 0) {
                continue;
            }
            pick.inspected += static_cast<int>(count);

            SDK::IRenderNode *chosen = nullptr;
            float chosenDistance     = 0.0F;
            float nearest            = finish;
            float nearestSize        = std::numeric_limits<float>::max();

            for (std::size_t index = 0; index < count; ++index) {
                SDK::IRenderNode *node = nodes[index];
                if (!IsPickableNode(node) || (plantsOnly && node->GetRenderNodeType() != SDK::eERType_Vegetation)) {
                    continue;
                }

                SDK::AABB bounds {};
                node->FillBBox(bounds);
                if (!IsUsableBounds(bounds)) {
                    continue;
                }

                // A large box around the camera is a building, not a target.
                if (BoxContains(bounds, origin) && BoxLongestSpan(bounds) > kEnclosingSpan) {
                    continue;
                }

                if (avoid != nullptr && BoxContains(bounds, *avoid)) {
                    continue;
                }

                if (BoxLongestSpan(bounds) > maxSpan) {
                    continue;
                }

                const float distance = SDK::RayBoxEntryDistance(origin, direction, bounds);
                if (distance < 0.0F || distance > nearest + kNearTieMetres) {
                    continue;
                }

                const float size = BoxVolume(bounds);
                if (distance > nearest - kNearTieMetres && size >= nearestSize) {
                    continue;
                }

                nearest        = std::min(nearest, distance);
                nearestSize    = size;
                chosen         = node;
                chosenDistance = distance;
            }

            if (chosen != nullptr) {
                const int inspected = pick.inspected;
                pick                = DescribeNode(chosen, origin, direction, chosenDistance);
                pick.boundsOnly     = true;
                pick.inspected      = inspected;
                return pick;
            }
        }

        return pick;
    }

    WorldPick WorldObjects::PickFromEntities(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, const SDK::Vec3 *avoid, float maxSpan) const {
        WorldPick pick;

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return pick;
        }

        SDK::IEntityItPtr iterator(entitySystem->GetEntityIterator());
        if (!iterator) {
            return pick;
        }

        SDK::EntityId localPlayerId = 0;
        auto *framework             = SDK::IGameFramework::GetInstance();
        if (SDK::IEntity *player = framework != nullptr ? framework->GetClientEntity() : nullptr) {
            localPlayerId = player->GetId();
        }

        SDK::EntityId chosen = 0;
        SDK::Vec3 chosenPoint {};
        float nearest     = maxDistance;
        float nearestSize = std::numeric_limits<float>::max();

        iterator->MoveFirst();
        while (!iterator->IsEnd()) {
            SDK::IEntity *entity = iterator->Next();
            if (entity == nullptr || entity->GetId() == localPlayerId) {
                continue;
            }

            // Runs over every entity: the cheap cylinder test goes first.
            const SDK::Vec3 position = entity->GetWorldPos();
            const SDK::Vec3 offset {position.x - origin.x, position.y - origin.y, position.z - origin.z};

            const float along = Dot(offset, direction);
            if (along < -kEntityStrayRadius || along > maxDistance + kEntityStrayRadius) {
                continue;
            }

            const float straySquared = Dot(offset, offset) - along * along;
            if (straySquared > kEntityStrayRadius * kEntityStrayRadius) {
                continue;
            }

            if (!IsSelectableEntity(entity)) {
                continue;
            }

            SDK::AABB bounds {};
            entity->GetWorldBounds(bounds);
            if (!IsUsableBounds(bounds)) {
                continue;
            }
            if (BoxContains(bounds, origin) && BoxLongestSpan(bounds) > kEnclosingSpan) {
                continue;
            }
            if (avoid != nullptr && BoxContains(bounds, *avoid)) {
                continue;
            }
            if (BoxLongestSpan(bounds) > maxSpan) {
                continue;
            }

            const float distance = SDK::RayBoxEntryDistance(origin, direction, bounds);
            if (distance < 0.0F || distance > nearest + kNearTieMetres) {
                continue;
            }

            const float size = BoxVolume(bounds);
            if (distance > nearest - kNearTieMetres && size >= nearestSize) {
                continue;
            }

            nearest     = std::min(nearest, distance);
            nearestSize = size;
            chosen      = entity->GetId();
            chosenPoint = SDK::Vec3 {origin.x + direction.x * distance, origin.y + direction.y * distance, origin.z + direction.z * distance};
        }

        if (chosen != 0) {
            pick.kind       = WorldPickKind::Entity;
            pick.boundsOnly = true;
            pick.entityId   = chosen;
            pick.hitPoint   = chosenPoint;
            pick.distance   = nearest;
            pick.foreignId  = SDK::PHYS_FOREIGN_ID_ENTITY;
        }
        return pick;
    }

    WorldPick WorldObjects::Pick(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, PickEffort effort, bool preferPhysics) const {
        WorldPick pick;

        auto *physics = SDK::CPhysicalWorld::GetInstance();
        if (physics == nullptr) {
            return PickFromOctree(origin, direction, maxDistance, nullptr, std::numeric_limits<float>::max());
        }

        const float openReach = effort == PickEffort::Thorough ? maxDistance : kQuickOpenReach;

        const SDK::Vec3 span {direction.x * maxDistance, direction.y * maxDistance, direction.z * maxDistance};

        // The player's body sits under the camera; skip it.
        SDK::IPhysicalEntity *skip[kMaxPierced + 1] {};
        int skipCount   = 0;
        auto *framework = SDK::IGameFramework::GetInstance();
        if (SDK::IEntity *player = framework != nullptr ? framework->GetClientEntity() : nullptr) {
            if (SDK::IPhysicalEntity *body = player->GetPhysics()) {
                skip[skipCount++] = body;
            }
        }

        for (int attempt = 0; attempt <= kMaxPierced; ++attempt) {
            SDK::SRayHit hit {};
            if (physics->RayWorldIntersection(origin, span, SDK::CPhysicalWorld::kAllObjectTypes, SDK::CPhysicalWorld::kPickRayFlags, &hit, 1, skipCount != 0 ? skip : nullptr, skipCount) <= 0) {
                break;
            }

            pick.hitPoint = hit.point;
            pick.distance = hit.distance;
            pick.kind     = WorldPickKind::Terrain;

            auto *collider = static_cast<SDK::IPhysicalEntity *>(hit.collider);
            if (collider == nullptr) {
                // Only terrain has no collider.
                break;
            }

            pick.foreignId = collider->GetiForeignData();

            bool offered = false;
            if (pick.foreignId == SDK::PHYS_FOREIGN_ID_STATIC) {
                auto *node = static_cast<SDK::IRenderNode *>(collider->GetForeignData(SDK::PHYS_FOREIGN_ID_STATIC));
                if (node == nullptr) {
                    if (const auto placement = KCDC::Features::World::LevelColliderModule::Get().FindPlacement(collider)) {
                        const char *path = placement->mesh->GetFilePath();
                        if (path != nullptr) {
                            node = FindLevelBrush(path, placement->position, _octreeScratch, &hit.point);
                            if (node == nullptr) {
                                node = FindLevelBrush(path, placement->position, _octreeScratch);
                            }
                        }
                    }
                }
                if (node != nullptr && IsPickableNode(node)) {
                    pick          = DescribeNode(node, origin, direction, hit.distance);
                    pick.hitPoint = hit.point;
                    offered       = true;
                }
            }
            else if (pick.foreignId == SDK::PHYS_FOREIGN_ID_ENTITY) {
                auto *entity = static_cast<SDK::IEntity *>(collider->GetForeignData(SDK::PHYS_FOREIGN_ID_ENTITY));
                if (IsSelectableEntity(entity)) {
                    pick.kind     = WorldPickKind::Entity;
                    pick.entityId = entity->GetId();
                    offered       = true;
                }
            }
            else if (hit.isTerrain != 0 || pick.foreignId == SDK::PHYS_FOREIGN_ID_TERRAIN) {
                break;
            }

            if (offered) {
                break;
            }

            // Solid but not offerable: step past it.
            pick.kind = WorldPickKind::Unknown;
            if (attempt == kMaxPierced || skipCount >= static_cast<int>(std::size(skip))) {
                break;
            }
            skip[skipCount++] = collider;
        }

        // Decoration is unphysicalized and hangs on solid things: search in front of the hit.
        const bool haveSolid  = pick.kind != WorldPickKind::None;
        const float limit     = haveSolid ? pick.distance + kInFrontSlack : openReach;
        const float spanLimit = haveSolid ? kDecorationSpan : std::numeric_limits<float>::max();

        WorldPick inFront = PickFromOctree(origin, direction, limit, haveSolid ? &pick.hitPoint : nullptr, spanLimit);
        pick.inspected    = inFront.inspected;

        if (inFront.kind == WorldPickKind::None) {
            // Entities with neither collider nor node; the costly pass.
            if (effort != PickEffort::Thorough) {
                return pick;
            }

            const WorldPick fromEntities = PickFromEntities(origin, direction, limit, haveSolid ? &pick.hitPoint : nullptr, spanLimit);
            if (fromEntities.kind == WorldPickKind::None) {
                return pick;
            }
            inFront           = fromEntities;
            inFront.inspected = pick.inspected;
        }

        if (IsSameLevelObject(inFront, pick)) {
            return pick;
        }

        return PreferWorldPick(inFront, pick, preferPhysics) ? inFront : pick;
    }

    SDK::IPhysicalEntity *WorldObjects::Collider(std::uint64_t id) {
        auto *edit = FindMutable(id);
        if (!edit || !Resolve(*edit)) {
            return nullptr;
        }
        if (edit->kind == WorldPickKind::RenderNode) {
            return edit->node->GetPhysics();
        }
        auto *system = SDK::IEntitySystem::GetInstance();
        auto *entity = system ? system->GetEntity(edit->entityId) : nullptr;
        return entity ? entity->GetPhysics() : nullptr;
    }

    void WorldObjects::IndexEdit(std::size_t at) const {
        if (!_indexValid) {
            return;
        }
        const WorldEdit &edit = _edits[at];
        _indexById[edit.id]   = at;
        if (edit.kind == WorldPickKind::RenderNode) {
            _indexByMesh.emplace(Framework::Utils::StringUtils::ToLower(edit.path), at);
        }
        else if (edit.kind == WorldPickKind::Entity) {
            if (edit.guid != 0) {
                _indexByGuid[edit.guid] = at;
            }
            if (edit.entityId != 0) {
                _indexByEntity[edit.entityId] = at;
            }
        }
    }

    void WorldObjects::EnsureIndex() const {
        if (_indexValid) {
            return;
        }
        _indexById.clear();
        _indexByMesh.clear();
        _indexByGuid.clear();
        _indexByEntity.clear();
        _indexValid = true;
        for (std::size_t at = 0; at < _edits.size(); ++at) {
            IndexEdit(at);
        }
    }

    const WorldEdit *WorldObjects::Find(std::uint64_t id) const {
        EnsureIndex();
        const auto found = _indexById.find(id);
        return found != _indexById.end() ? &_edits[found->second] : nullptr;
    }

    WorldEdit *WorldObjects::FindMutable(std::uint64_t id) {
        return const_cast<WorldEdit *>(static_cast<const WorldObjects *>(this)->Find(id));
    }

    std::string WorldObjects::MeshPath(const WorldEdit &edit) {
        if (edit.kind == WorldPickKind::RenderNode) {
            return edit.path;
        }
        if (edit.kind != WorldPickKind::Entity) {
            return {};
        }
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(edit.entityId) : nullptr;
        if (entity == nullptr) {
            return {};
        }
        SDK::IStatObj *mesh = entity->GetStatObj(kCompoundStatObjSlot);
        if (mesh == nullptr) {
            mesh = entity->GetStatObj(0);
        }
        const char *path = mesh != nullptr ? mesh->GetFilePath() : nullptr;
        return path != nullptr ? std::string {path} : std::string {};
    }

    std::uint64_t WorldObjects::FindByPick(const WorldPick &pick) const {
        // By key: a brush pointer an edit resolved earlier may since have been
        // freed and its address reused by another node.
        std::string path;
        SDK::Vec3 at {};
        std::uint64_t guid = 0;
        if (pick.kind == WorldPickKind::RenderNode && pick.node != nullptr) {
            const char *name            = pick.node->GetName();
            path                        = name != nullptr ? name : "";
            const SDK::Matrix34 &matrix = pick.node->GetMatrix();
            at                          = SDK::Vec3 {matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
        }
        else if (pick.kind == WorldPickKind::Entity) {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr;
            guid                 = entity != nullptr ? entity->GetGuid() : 0;
        }

        EnsureIndex();
        if (pick.kind == WorldPickKind::RenderNode) {
            const auto [first, last] = _indexByMesh.equal_range(Framework::Utils::StringUtils::ToLower(path));
            for (auto entry = first; entry != last; ++entry) {
                const WorldEdit &edit = _edits[entry->second];
                const SDK::Vec3 offset {edit.position.x - at.x, edit.position.y - at.y, edit.position.z - at.z};
                if (Dot(offset, offset) <= kBrushKeySlack * kBrushKeySlack) {
                    return edit.id;
                }
            }
            return 0;
        }
        if (pick.kind != WorldPickKind::Entity) {
            return 0;
        }
        if (guid != 0) {
            if (const auto found = _indexByGuid.find(guid); found != _indexByGuid.end()) {
                return _edits[found->second].id;
            }
        }
        const auto found = _indexByEntity.find(pick.entityId);
        return found != _indexByEntity.end() && _edits[found->second].entityId == pick.entityId ? _edits[found->second].id : 0;
    }

    bool WorldObjects::CanAdopt(const WorldPick &pick) const {
        if (pick.kind != WorldPickKind::RenderNode && pick.kind != WorldPickKind::Entity) {
            return false;
        }
        if (FindByPick(pick) != 0) {
            return true;
        }
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        return pick.kind == WorldPickKind::RenderNode ? IsPickableNode(pick.node) : IsSelectableEntity(entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr);
    }

    bool WorldObjects::LevelPose(const WorldPick &pick, SDK::Vec3 &at, SDK::Quat &rotation, SDK::Vec3 &scale) const {
        if (pick.kind == WorldPickKind::RenderNode) {
            if (pick.node == nullptr || pick.node->GetOwnerEntity() != nullptr || KCDC::Common::Spawn::StaticBrushRegistry::Get().Owns(pick.node)) {
                return false;
            }
            if (KCDC::Core::gApplication && KCDC::Core::gApplication->GetPropGhostService().OwnsNode(pick.node)) {
                return false;
            }
        }
        else if (pick.kind == WorldPickKind::Entity) {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr;
            if (entity == nullptr || entity->GetGuid() == 0) {
                return false;
            }
        }
        else {
            return false;
        }

        if (const WorldEdit *edit = Find(FindByPick(pick)); edit != nullptr && edit->originalKnown) {
            at       = edit->originalPosition;
            rotation = edit->originalRotation;
            scale    = edit->originalScale;
            return true;
        }

        if (pick.kind == WorldPickKind::RenderNode) {
            SDK::Matrix34ToTransform(pick.node->GetMatrix(), at, rotation, scale);
            return true;
        }
        SDK::IEntity *entity = SDK::IEntitySystem::GetInstance()->GetEntity(pick.entityId);
        at                   = entity->GetPos();
        rotation             = entity->GetRotation();
        scale                = entity->GetScale();
        return true;
    }

    SDK::IRenderNode *WorldObjects::FindLevelBrushAt(const std::string &path, const SDK::Vec3 &position) {
        std::vector<SDK::IRenderNode *> scratch;
        return FindLevelBrush(path, position, scratch);
    }

    std::uint64_t WorldObjects::Adopt(const WorldPick &pick) {
        if (!CanAdopt(pick)) {
            return 0;
        }
        if (const std::uint64_t existing = FindByPick(pick)) {
            return existing;
        }

        WorldEdit edit;
        edit.id       = _nextId++;
        edit.kind     = pick.kind;
        edit.node     = pick.node;
        edit.entityId = pick.entityId;

        if (pick.kind == WorldPickKind::RenderNode) {
            SDK::Matrix34 matrix = pick.node->GetMatrix();
            SDK::Matrix34ToTransform(matrix, edit.position, edit.rotation, edit.scale);

            const char *name     = pick.node->GetName();
            edit.path            = name != nullptr ? name : "";
            edit.layerId         = pick.node->GetLayerId();
            edit.onSwitchedLayer = (pick.node->GetRenderFlags() & SDK::ERF_ACTIVE_LAYER) != 0;

            SDK::AABB bounds {};
            pick.node->FillBBox(bounds);
            edit.hasBounds = IsUsableBounds(bounds);
            if (edit.hasBounds) {
                edit.worldBounds = bounds;
            }
        }
        else {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr;
            if (entity == nullptr) {
                return 0;
            }

            edit.position = entity->GetPos();
            edit.rotation = entity->GetRotation();
            edit.scale    = entity->GetScale();

            SDK::AABB bounds {};
            entity->GetWorldBounds(bounds);
            edit.hasBounds = IsUsableBounds(bounds);
            if (edit.hasBounds) {
                edit.worldBounds = bounds;
            }

            const char *name               = entity->GetName();
            SDK::IEntityClass *entityClass = entity->GetClass();
            edit.guid                      = entity->GetGuid();
            edit.path                      = name != nullptr ? name : "";
            edit.className                 = entityClass != nullptr && entityClass->GetName() != nullptr ? entityClass->GetName() : "";
            edit.hidden                    = entity->IsHidden();
        }

        DescribeWorldPick(pick, edit.label, edit.detail);

        edit.originalPosition = edit.position;
        edit.originalRotation = edit.rotation;
        edit.originalScale    = edit.scale;

        _edits.push_back(std::move(edit));
        IndexEdit(_edits.size() - 1);
        return _edits.back().id;
    }

    bool WorldObjects::ApplyTransform(const WorldEdit &edit) {
        if (edit.kind == WorldPickKind::RenderNode) {
            if (edit.node == nullptr) {
                return false;
            }
            // SetMatrix re-registers, and physicalizes any brush without a
            // collider -- hidden ones too, which would leave an invisible wall.
            edit.node->SetMatrix(SDK::Matrix34FromTransform(edit.position, edit.rotation, edit.scale));
            if (edit.node->IsHidden()) {
                edit.node->Dephysicalize(false);
            }
            return true;
        }

        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr && edit.entityId != 0 ? entitySystem->GetEntity(edit.entityId) : nullptr;
        if (entity == nullptr) {
            return false;
        }
        entity->SetPosRotScale(edit.position, edit.rotation, edit.scale);
        return true;
    }

    void WorldObjects::ApplyHidden(const WorldEdit &edit, bool hidden) {
        if (edit.kind == WorldPickKind::Entity) {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(edit.entityId) : nullptr;
            // The physics proxy disables itself on the hide event.
            if (entity != nullptr && entity->IsHidden() != hidden) {
                entity->Hide(hidden);
            }
            return;
        }

        if (edit.node == nullptr || edit.node->IsHidden() == hidden) {
            return;
        }
        // Collision goes with the mesh, or the brush is an invisible wall. For
        // a layer brush this is the brush's own collider, which it has only
        // while SyncCollision holds the layer's out.
        edit.node->SetHidden(hidden);
        if (hidden) {
            edit.node->Dephysicalize(false);
        }
        else {
            edit.node->PhysicalizeNow(true);
        }
    }

    void WorldObjects::SyncCollision(WorldEdit &edit) {
        if (edit.kind != WorldPickKind::RenderNode || !edit.loaded) {
            return;
        }
        if (edit.IsRemoved() || edit.moved) {
            HoldCollision(edit);
        }
        else {
            ReleaseCollision(edit);
        }
    }

    void WorldObjects::HoldCollision(WorldEdit &edit) {
        // Only a brush the layer physics collides for: any other already owns
        // its collision, and hiding or moving it takes that along natively.
        SDK::IRenderNode *node = edit.node;
        if (node == nullptr || node->GetRenderNodeType() != SDK::eERType_Brush || (node->GetRenderFlags() & SDK::ERF_LAYER_PHYSICS) == 0) {
            return;
        }

        // Held once per edit; a fresh HLOD instance of the same brush comes
        // back with the flag set and only needs handing its collision again.
        if (edit.collisionHeldFor == nullptr) {
            SDK::IStatObj *statObj = static_cast<SDK::CBrush *>(node)->GetStatObj();
            if (!KCDC::Features::World::LevelColliderModule::Get().Hold(statObj, edit.originalPosition)) {
                return;
            }
            edit.collisionHeldFor = statObj;
        }

        // Cleared, CBrush::PhysicalizeOnHeap builds the stock collider, which
        // SetMatrix then moves and Dephysicalize drops. A hidden brush gets
        // none, whether the edit or a switched-off layer hid it: the layer
        // switch physicalizes it itself when it shows it again. Instant, or a
        // small brush is only queued for physics on demand.
        node->SetLayerPhysics(false);
        if (!edit.IsRemoved() && !node->IsHidden()) {
            node->PhysicalizeNow(true);
        }
    }

    void WorldObjects::ReleaseCollision(WorldEdit &edit) {
        if (edit.collisionHeldFor == nullptr) {
            return;
        }
        SDK::IStatObj *statObj = edit.collisionHeldFor;
        edit.collisionHeldFor  = nullptr;

        // Another holder still keeps the layer collider out, so the brush
        // keeps colliding for itself.
        if (!KCDC::Features::World::LevelColliderModule::Get().Release(statObj, edit.originalPosition)) {
            return;
        }
        if (edit.loaded && edit.node != nullptr) {
            edit.node->Dephysicalize(false);
            edit.node->SetLayerPhysics(true);
        }
    }

    bool WorldObjects::Resolve(WorldEdit &edit) {
        if (edit.kind == WorldPickKind::Entity) {
            edit.entityRenderNode = nullptr;
            auto *entitySystem    = SDK::IEntitySystem::GetInstance();
            if (entitySystem == nullptr) {
                edit.loaded = false;
                return false;
            }

            // A layer that streams out and back in makes a new entity under
            // the same level GUID; the id is only a cache of it.
            const SDK::EntityId previous = edit.entityId;
            if (edit.guid != 0) {
                edit.entityId = entitySystem->FindEntityByGuid(edit.guid);
            }
            if (edit.entityId != previous) {
                _indexValid = false;
            }
            SDK::IEntity *entity = edit.entityId != 0 ? entitySystem->GetEntity(edit.entityId) : nullptr;
            if (entity == nullptr || entity->IsGarbage()) {
                edit.loaded = false;
                return false;
            }

            auto *render          = static_cast<SDK::IEntityRenderProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)));
            edit.entityRenderNode = render != nullptr ? render->GetRenderNode() : nullptr;

            const bool fresh = !edit.loaded || edit.entityId != previous;
            edit.loaded      = true;
            if (!edit.originalKnown) {
                edit.originalRotation = entity->GetRotation();
                edit.originalScale    = entity->GetScale();
                edit.originalKnown    = true;
            }
            if (fresh && edit.moved) {
                ApplyTransform(edit);
            }
            // Quest scripts show their own props again; the editor overrules them.
            if (edit.IsRemoved()) {
                ApplyHidden(edit, true);
            }
            SyncReplacement(edit);
            return true;
        }

        // Where the brush stands now, or, for a moved one that streamed back
        // in, where the level puts a fresh copy.
        SDK::IRenderNode *node = FindLevelBrush(edit.path, edit.position, _octreeScratch);
        if (node == nullptr && edit.moved) {
            node = FindLevelBrush(edit.path, edit.originalPosition, _octreeScratch);
        }
        if (node == nullptr) {
            edit.node   = nullptr;
            edit.loaded = false;
            return false;
        }

        const bool fresh = !edit.loaded || node != edit.node;
        edit.node        = node;
        edit.loaded      = true;
        edit.layerId     = node->GetLayerId();

        // An edit imported while the brush was streamed out learns the
        // authored rotation and scale off its first sight of it.
        if (!edit.originalKnown) {
            SDK::Vec3 position {};
            SDK::Matrix34ToTransform(node->GetMatrix(), position, edit.originalRotation, edit.originalScale);
            edit.originalKnown = true;
            if (!edit.moved) {
                edit.rotation = edit.originalRotation;
                edit.scale    = edit.originalScale;
            }
        }

        // The layer system flips ERF_ACTIVE_LAYER on the brushes it switches,
        // and nothing else touches it.
        const bool activeLayer = (node->GetRenderFlags() & SDK::ERF_ACTIVE_LAYER) != 0;
        edit.onSwitchedLayer   = edit.onSwitchedLayer || activeLayer;
        edit.layerOff          = edit.onSwitchedLayer && !activeLayer;

        // Before the move, so SetMatrix finds a brush that physicalizes itself.
        SyncCollision(edit);

        if (fresh && edit.moved) {
            ApplyTransform(edit);
        }
        // A fresh HLOD instance, or a layer switched back on, shows it again.
        if (edit.IsRemoved()) {
            ApplyHidden(edit, true);
        }
        SyncReplacement(edit);
        return true;
    }

    bool WorldObjects::SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale) {
        WorldEdit *edit = FindMutable(id);
        if (edit == nullptr || !KCDC::Common::IsFinite(position) || !KCDC::Common::IsFinite(scale)) {
            return false;
        }

        // Found where it stands before the pose changes, since the key is the pose.
        const bool loaded = Resolve(*edit);

        edit->position = position;
        edit->rotation = rotation;
        edit->scale    = ClampObjectScale(scale);

        // Back where it was found is not a move, so an undone drag is not saved.
        const auto same = [](const SDK::Vec3 &a, const SDK::Vec3 &b) {
            return std::fabs(a.x - b.x) < 1e-3F && std::fabs(a.y - b.y) < 1e-3F && std::fabs(a.z - b.z) < 1e-3F;
        };
        const float alignment = std::fabs(edit->rotation.x * edit->originalRotation.x + edit->rotation.y * edit->originalRotation.y + edit->rotation.z * edit->originalRotation.z + edit->rotation.w * edit->originalRotation.w);
        edit->moved           = !same(edit->position, edit->originalPosition) || !same(edit->scale, edit->originalScale) || alignment < 0.99999F;

        // The collision changes hands before the move, so the brush's own
        // collider is the one SetMatrix carries along.
        if (loaded) {
            SyncCollision(*edit);
            if (ApplyTransform(*edit)) {
                ReadBack(*edit);
            }
        }
        // A swap moves with the object it stands in for.
        SyncReplacement(*edit);
        return true;
    }

    bool WorldObjects::SetHidden(std::uint64_t id, bool hidden) {
        WorldEdit *edit = FindMutable(id);
        if (edit == nullptr) {
            return false;
        }

        // Not loaded: the intent is kept and applied when it streams in.
        edit->hidden = hidden;
        if (!Resolve(*edit)) {
            return true;
        }

        // A brush on a layer that is off stays hidden: the level hid it, and
        // shows it again itself when the layer comes back on. A swapped one
        // stays out whatever `hidden` says.
        if (!edit->IsRemoved() && edit->layerOff) {
            return true;
        }
        ApplyHidden(*edit, edit->IsRemoved());
        return true;
    }

    bool WorldObjects::IsReplacementModel(const std::string &model) {
        if (Shared::Game::FindPropModel(model) != nullptr) {
            return true;
        }
        // A streamed mesh this client has not mounted yet is held, and built
        // once it is; `SyncReplacement` waits for it.
        return KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath(model);
    }

    bool WorldObjects::SetReplacement(std::uint64_t id, const std::string &model) {
        WorldEdit *edit = FindMutable(id);
        if (edit == nullptr || (!model.empty() && !IsReplacementModel(model))) {
            return false;
        }
        if (edit->replacement == model) {
            return true;
        }

        const bool wasRemoved    = edit->IsRemoved();
        edit->replacement        = model;
        edit->replacementRefused = false;

        // Not loaded: the swap is kept and made when it streams in. Resolve
        // stands or takes down the mesh either way.
        if (!Resolve(*edit)) {
            SyncReplacement(*edit);
            return true;
        }

        // Whether the level's own collider stays held out turns on the swap too.
        SyncCollision(*edit);
        if (wasRemoved != edit->IsRemoved() && !(edit->layerOff && !edit->IsRemoved())) {
            ApplyHidden(*edit, edit->IsRemoved());
        }
        return true;
    }

    void WorldObjects::SyncReplacement(WorldEdit &edit) {
        if (edit.replacement.empty()) {
            edit.replacementBrush.Destroy();
            edit.replacementBuilt.clear();
            return;
        }

        // The swap stands where the object does, so it waits for the first
        // sight of an object imported while it was streamed out.
        if (!edit.originalKnown) {
            return;
        }

        const SDK::Matrix34 matrix = SDK::Matrix34FromTransform(edit.position, edit.rotation, edit.scale);
        if (edit.replacementBrush.IsBuilt() && edit.replacementBuilt == edit.replacement) {
            const auto closeTo = [](const SDK::Vec3 &a, const SDK::Vec3 &b) {
                return std::fabs(a.x - b.x) < 1e-4F && std::fabs(a.y - b.y) < 1e-4F && std::fabs(a.z - b.z) < 1e-4F;
            };
            const SDK::Quat &a   = edit.rotation;
            const SDK::Quat &b   = edit.replacementRotation;
            const bool samePlace = closeTo(edit.position, edit.replacementPosition) && closeTo(edit.scale, edit.replacementScale) && std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) > 0.99999F;
            if (!samePlace) {
                edit.replacementBrush.SetMatrix(matrix);
                edit.replacementPosition = edit.position;
                edit.replacementRotation = edit.rotation;
                edit.replacementScale    = edit.scale;
            }
            return;
        }
        if (edit.replacementRefused) {
            return;
        }

        // A streamed mesh this client has not mounted yet: held until it is.
        if (Shared::Game::FindPropModel(edit.replacement) == nullptr && !KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(edit.replacement)) {
            return;
        }

        switch (edit.replacementBrush.Build(edit.replacement.c_str(), matrix, true)) {
        case KCDC::Common::Spawn::StaticBrush::BuildResult::NoEngine: return;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::MeshRefused:
            edit.replacementRefused = true;
            Framework::Logging::GetLogger("WorldObjects")->error("Could not swap '{}' for '{}': the game would not load the mesh", edit.path, edit.replacement);
            return;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::BuiltWithoutCollider: Framework::Logging::GetLogger("WorldObjects")->warn("'{}' stands in place of '{}' without collision: the mesh has no physics geometry", edit.replacement, edit.path); break;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::Built: break;
        }
        edit.replacementBuilt    = edit.replacement;
        edit.replacementPosition = edit.position;
        edit.replacementRotation = edit.rotation;
        edit.replacementScale    = edit.scale;
    }

    void WorldObjects::PutBack(WorldEdit &edit) {
        // The swap is the edit's own mesh, so it goes whether or not the
        // object is loaded, and before Resolve could stand it again.
        const bool removed = edit.IsRemoved();
        edit.replacement.clear();
        edit.replacementRefused = false;
        SyncReplacement(edit);

        // Not loaded: the level brings it back as authored anyway, collider
        // and all once the hold on it goes.
        if (!Resolve(edit)) {
            ReleaseCollision(edit);
            return;
        }
        if (removed && !edit.layerOff) {
            ApplyHidden(edit, false);
        }
        if (edit.moved) {
            edit.position = edit.originalPosition;
            edit.rotation = edit.originalRotation;
            edit.scale    = edit.originalScale;
            ApplyTransform(edit);
        }
        ReleaseCollision(edit);
    }

    bool WorldObjects::Restore(std::uint64_t id, bool keepIdentity) {
        const auto found = std::find_if(_edits.begin(), _edits.end(), [id](const WorldEdit &edit) {
            return edit.id == id;
        });
        if (found == _edits.end()) {
            return false;
        }
        PutBack(*found);
        if (keepIdentity) {
            found->hidden   = false;
            found->moved    = false;
            // PutBack already dropped the swap.
            found->position = found->originalPosition;
            found->rotation = found->originalRotation;
            found->scale    = found->originalScale;
        }
        else {
            _edits.erase(found);
            _indexValid = false;
        }
        return true;
    }

    std::unordered_set<std::uint64_t> WorldObjects::ReleaseUntouched(const std::unordered_set<std::uint64_t> &ids) {
        // An untouched record holds no collider and changed nothing in the world, so dropping it is enough.
        std::unordered_set<std::uint64_t> released;
        std::erase_if(_edits, [&ids, &released](const WorldEdit &edit) {
            const bool untouched = ids.contains(edit.id) && !edit.IsRemoved() && !edit.moved && edit.collisionHeldFor == nullptr;
            if (untouched) {
                released.insert(edit.id);
            }
            return untouched;
        });
        if (!released.empty()) {
            _indexValid    = false;
            _refreshCursor = 0;
        }
        return released;
    }

    void WorldObjects::RestoreAll() {
        for (auto it = _edits.rbegin(); it != _edits.rend(); ++it) {
            PutBack(*it);
        }
        _edits.clear();
        _indexValid = false;
    }

    void WorldObjects::Forget() {
        // The brushes may be freed memory, so only the holds are let go of:
        // the level that went took its colliders with it, and a hold on one
        // of them would otherwise outlive it.
        for (WorldEdit &edit : _edits) {
            if (edit.collisionHeldFor != nullptr) {
                KCDC::Features::World::LevelColliderModule::Get().Release(edit.collisionHeldFor, edit.originalPosition);
                edit.collisionHeldFor = nullptr;
            }
            // Its octree went with the level, and its collider with the physical world's entities.
            edit.replacementBrush.Forget();
        }
        _edits.clear();
        _indexValid = false;
    }

    void WorldObjects::ReadBack(WorldEdit &edit) {
        SDK::AABB bounds {};
        if (edit.kind == WorldPickKind::Entity) {
            SDK::IEntity *entity = SDK::IEntitySystem::GetInstance()->GetEntity(edit.entityId);
            edit.position        = entity->GetPos();
            edit.rotation        = entity->GetRotation();
            edit.scale           = entity->GetScale();
            entity->GetWorldBounds(bounds);
        }
        else {
            edit.node->FillBBox(bounds);
        }
        edit.hasBounds = IsUsableBounds(bounds);
        if (edit.hasBounds) {
            edit.worldBounds = bounds;
        }
    }

    void WorldObjects::Refresh() {
        // Entities are a GUID lookup, so every one is re-found every tick.
        for (auto it = _edits.begin(); it != _edits.end();) {
            if (it->kind != WorldPickKind::Entity) {
                ++it;
                continue;
            }
            if (Resolve(*it)) {
                ReadBack(*it);
            }
            else if (it->guid == 0) {
                // Not authored by the level: no key to come back by.
                it          = _edits.erase(it);
                _indexValid = false;
                continue;
            }
            ++it;
        }

        const std::size_t total = _edits.size();
        std::size_t visited     = 0;
        std::size_t resolved    = 0;
        for (; visited < total && resolved < kBrushResolvesPerTick; ++visited) {
            WorldEdit &edit = _edits[(_refreshCursor + visited) % total];
            if (edit.kind != WorldPickKind::RenderNode) {
                continue;
            }
            ++resolved;
            if (Resolve(edit)) {
                ReadBack(edit);
            }
        }
        _refreshCursor = total != 0 ? (_refreshCursor + visited) % total : 0;
    }

    const SDK::IRenderNode *WorldObjects::ResolveRenderNode(std::uint64_t id) {
        auto *edit = FindMutable(id);
        if (!edit || !Resolve(*edit)) {
            return nullptr;
        }
        if (edit->kind == WorldPickKind::RenderNode) {
            return edit->node;
        }
        return edit->entityRenderNode;
    }

    bool WorldObjects::SupportsLayerVisibility(std::uint64_t id) {
        auto *edit = FindMutable(id);
        if (edit == nullptr) {
            return false;
        }
        if (edit->kind == WorldPickKind::Entity) {
            return true;
        }
        if (!Resolve(*edit)) {
            return false;
        }
        // These layouts expose a stored position safe to read from a live native render
        // argument. Other bare node types may implement GetMatrix using shared scratch.
        switch (edit->node->GetRenderNodeType()) {
        case SDK::eERType_Brush:
        case SDK::eERType_MovableBrush:
        case SDK::eERType_Vegetation: return true;
        default: return false;
        }
    }

    bool WorldObjects::ToDefinition(const WorldEdit &edit, Shared::Game::LevelEditDefinition &entry, bool identityOnly) {
        if (!identityOnly && !edit.IsRemoved() && !edit.moved) {
            return false;
        }

        // Not authored by the level: no GUID another run would know.
        if (edit.kind == WorldPickKind::Entity && edit.guid == 0) {
            return false;
        }

        entry           = Shared::Game::LevelEditDefinition {};
        entry.kind      = edit.kind == WorldPickKind::Entity ? Shared::Game::LevelEditKind::Entity : Shared::Game::LevelEditKind::Brush;
        entry.guid      = edit.guid;
        entry.name      = edit.path;
        entry.className = edit.className;
        entry.at        = KCDC::Common::ToGlm(edit.originalPosition);
        entry.hidden    = !identityOnly && edit.hidden;
        entry.model     = identityOnly ? std::string() : edit.replacement;
        entry.moved     = !identityOnly && edit.moved;
        entry.position  = KCDC::Common::ToGlm(edit.position);
        entry.rotation  = KCDC::Common::ToGlm(edit.rotation);
        entry.scale     = KCDC::Common::ToGlm(edit.scale);
        return true;
    }

    void WorldObjects::ToDocument(std::vector<Shared::Game::LevelEditDefinition> &edits) const {
        edits.clear();
        for (const WorldEdit &edit : _edits) {
            Shared::Game::LevelEditDefinition entry;
            if (ToDefinition(edit, entry)) {
                edits.push_back(std::move(entry));
            }
        }
    }

    std::uint64_t WorldObjects::ApplyDocument(const Shared::Game::LevelEditDefinition &entry) {
        if (!entry.HasKey()) {
            return 0;
        }
        const bool isEntity              = entry.kind == Shared::Game::LevelEditKind::Entity;
        const SDK::Vec3 originalPosition = KCDC::Common::ToNative(entry.at);

        // An edit already keyed the same way is reused, so a document applied
        // twice adds nothing.
        const auto existing = std::find_if(_edits.begin(), _edits.end(), [&](const WorldEdit &edit) {
            if (isEntity) {
                return edit.kind == WorldPickKind::Entity && edit.guid == entry.guid;
            }
            const SDK::Vec3 offset {edit.originalPosition.x - originalPosition.x, edit.originalPosition.y - originalPosition.y, edit.originalPosition.z - originalPosition.z};
            return edit.kind == WorldPickKind::RenderNode && _stricmp(edit.path.c_str(), entry.name.c_str()) == 0 && Dot(offset, offset) <= kBrushKeySlack * kBrushKeySlack;
        });

        std::uint64_t id = 0;
        if (existing != _edits.end()) {
            id = existing->id;
        }
        else {
            // Recorded whether or not the object is loaded: one streamed out
            // gets the edit when it streams in.
            WorldEdit edit;
            edit.id               = _nextId++;
            edit.kind             = isEntity ? WorldPickKind::Entity : WorldPickKind::RenderNode;
            edit.guid             = entry.guid;
            edit.path             = entry.name;
            edit.className        = entry.className;
            edit.position         = originalPosition;
            edit.originalPosition = originalPosition;
            edit.loaded           = false;
            edit.label            = isEntity ? (entry.name.empty() ? std::string {"(unnamed)"} : entry.name) : StemOf(entry.name);
            edit.detail           = isEntity ? "Entity  " + entry.className : "Brush  " + entry.name;

            edit.originalKnown = false;
            Resolve(edit);
            _edits.push_back(std::move(edit));
            IndexEdit(_edits.size() - 1);
            id = _edits.back().id;
        }

        if (entry.moved) {
            SetTransform(id, KCDC::Common::ToNative(entry.position), KCDC::Common::ToNative(entry.rotation), KCDC::Common::ToNative(entry.scale));
            // Against originals not yet seen, a pure turn would read as unmoved.
            WorldEdit *edit = FindMutable(id);
            edit->moved     = edit->moved || !edit->originalKnown;
        }
        if (entry.hidden) {
            SetHidden(id, true);
        }
        if (!entry.model.empty()) {
            SetReplacement(id, entry.model);
        }
        return id;
    }

    void WorldObjects::Collect(const SDK::AABB &box, std::vector<LevelCandidate> &candidates, bool &truncated) const {
        candidates.clear();
        truncated         = false;
        const auto inside = [&box](const SDK::Vec3 &point) {
            return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y && point.z >= box.min.z && point.z <= box.max.z;
        };

        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            SDK::EntityId localPlayerId = 0;
            auto *framework             = SDK::IGameFramework::GetInstance();
            if (SDK::IEntity *player = framework != nullptr ? framework->GetClientEntity() : nullptr) {
                localPlayerId = player->GetId();
            }

            SDK::IEntityItPtr iterator(entitySystem->GetEntityIterator());
            if (iterator) {
                iterator->MoveFirst();
                while (!iterator->IsEnd()) {
                    SDK::IEntity *entity = iterator->Next();
                    if (entity == nullptr || entity->GetId() == localPlayerId || entity->IsGarbage()) {
                        continue;
                    }
                    // Hidden ones are listed, so what was taken out can be found.
                    if (!inside(entity->GetWorldPos()) || (!entity->IsHidden() && !IsSelectableEntity(entity))) {
                        continue;
                    }
                    candidates.push_back(EntityCandidate(entity));
                }
            }
        }

        CollectNodes(box, box, 0, candidates, truncated);
    }

    void WorldObjects::CollectNodes(const SDK::AABB &box, const SDK::AABB &outer, int depth, std::vector<LevelCandidate> &candidates, bool &truncated) const {
        // A city block holds tens of thousands of nodes; eight levels of halving keep any sane box queryable.
        constexpr int kMaxSplits = 8;
        bool overflowed          = false;
        const std::size_t count  = QueryLevelOctree(box, _octreeScratch, &overflowed);
        if (overflowed) {
            if (depth >= kMaxSplits) {
                truncated = true;
                return;
            }
            const SDK::Vec3 middle {(box.min.x + box.max.x) * 0.5F, (box.min.y + box.max.y) * 0.5F, (box.min.z + box.max.z) * 0.5F};
            for (int octant = 0; octant < 8; ++octant) {
                const SDK::AABB part {SDK::Vec3 {(octant & 1) != 0 ? middle.x : box.min.x, (octant & 2) != 0 ? middle.y : box.min.y, (octant & 4) != 0 ? middle.z : box.min.z},
                    SDK::Vec3 {(octant & 1) != 0 ? box.max.x : middle.x, (octant & 2) != 0 ? box.max.y : middle.y, (octant & 4) != 0 ? box.max.z : middle.z}};
                CollectNodes(part, outer, depth + 1, candidates, truncated);
            }
            return;
        }
        // A node overlapping several parts is listed by each; only the part holding its position keeps it.
        const auto owns = [&box, &outer](const SDK::Vec3 &point) {
            const auto axis = [](float value, float low, float high, float outerHigh) {
                return value >= low && (value < high || (high >= outerHigh && value <= high));
            };
            return axis(point.x, box.min.x, box.max.x, outer.max.x) && axis(point.y, box.min.y, box.max.y, outer.max.y) && axis(point.z, box.min.z, box.max.z, outer.max.z);
        };
        for (std::size_t index = 0; index < count; ++index) {
            SDK::IRenderNode *node = _octreeScratch[index];
            if (!IsCollectableNode(node)) {
                continue;
            }
            const SDK::Vec3 position = NodePosition(node);
            if (!owns(position)) {
                continue;
            }
            candidates.push_back(NodeCandidate(node, position));
        }
    }

    bool WorldObjects::Describe(const WorldPick &pick, LevelCandidate &candidate) const {
        if (pick.kind == WorldPickKind::Entity) {
            auto *entitySystem   = SDK::IEntitySystem::GetInstance();
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(pick.entityId) : nullptr;
            if (entity == nullptr || entity->IsGarbage() || (!entity->IsHidden() && !IsSelectableEntity(entity))) {
                return false;
            }
            candidate = EntityCandidate(entity);
            return true;
        }
        if (pick.kind != WorldPickKind::RenderNode || !IsCollectableNode(pick.node)) {
            return false;
        }
        candidate = NodeCandidate(pick.node, NodePosition(pick.node));
        return true;
    }

    void WorldObjects::Browse(const SDK::Vec3 &origin, float radius, std::size_t maxRows, std::vector<LevelRow> &rows, bool &truncated) const {
        rows.clear();
        const SDK::AABB box {SDK::Vec3 {origin.x - radius, origin.y - radius, origin.z - radius}, SDK::Vec3 {origin.x + radius, origin.y + radius, origin.z + radius}};
        std::vector<LevelCandidate> candidates;
        Collect(box, candidates, truncated);

        const float radiusSquared = radius * radius;
        for (const LevelCandidate &candidate : candidates) {
            const SDK::Vec3 offset {candidate.position.x - origin.x, candidate.position.y - origin.y, candidate.position.z - origin.z};
            const float squared = Dot(offset, offset);
            if (squared > radiusSquared) {
                continue;
            }
            LevelRow row;
            row.kind     = candidate.pick.kind;
            row.node     = candidate.pick.node;
            row.entityId = candidate.pick.entityId;
            row.position = candidate.position;
            row.distance = std::sqrt(squared);
            row.hidden   = candidate.hidden;
            DescribeWorldPick(candidate.pick, row.label, row.detail);
            row.edited = FindByPick(candidate.pick) != 0;
            rows.push_back(std::move(row));
        }

        std::sort(rows.begin(), rows.end(), [](const LevelRow &left, const LevelRow &right) {
            return left.distance < right.distance;
        });
        if (rows.size() > maxRows) {
            rows.resize(maxRows);
            truncated = true;
        }
    }

    bool WorldObjects::IsNodeStanding(SDK::IRenderNode *node, const SDK::Vec3 &position) const {
        if (node == nullptr) {
            return false;
        }

        // Asked of the octree: the node itself may be freed memory.
        SDK::AABB box {};
        box.min = SDK::Vec3 {position.x - 0.5F, position.y - 0.5F, position.z - 0.5F};
        box.max = SDK::Vec3 {position.x + 0.5F, position.y + 0.5F, position.z + 0.5F};

        const std::size_t count = QueryOctree(box);
        return std::find(_octreeScratch.begin(), _octreeScratch.begin() + static_cast<std::ptrdiff_t>(count), node) != _octreeScratch.begin() + static_cast<std::ptrdiff_t>(count);
    }
} // namespace KCDC::Features::Prop
