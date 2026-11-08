#pragma once

#include "core/feature_registry.h"

#include "shared/features/world/world_query_rpc.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::World {
    /**
     * What a ray meets, what the ground is under a point, and which entities
     * are inside a sphere -- each the engine's own query: RayWorldIntersection
     * for the traces, QueryProximity for the sphere. The ground is a downward
     * trace rather than a terrain height, which is what puts a player on a
     * bridge instead of under it.
     *
     * Two callers, one implementation: the client's `World` binding calls
     * straight in, and the server's queries arrive as RPCs and are answered
     * from `Update`. Game thread throughout.
     */
    class WorldQueryService final: public KCDC::Core::IFeatureService {
      public:
        struct RayHit {
            SDK::Vec3 point {};
            SDK::Vec3 normal {0.0F, 0.0F, 1.0F};
            float distance = 0.0F;
            /** This machine's handle, 0 for terrain and static geometry. */
            SDK::EntityId entityId = 0;
            /** The level's own identity, 0 for anything the session spawned. */
            SDK::EntityGUID entityGuid = 0;
            std::string entityName;
            std::string entityClass;
            std::string surface;
            /** The `.cgf` of a vegetation instance or a brush; empty for terrain and entities. */
            std::string model;
            /** The (sub-)material drawn where the ray met it; empty when nothing names one. */
            std::string material;
            KCDC::Shared::RPC::WorldHitKind kind = KCDC::Shared::RPC::WorldHitKind::Static;
            bool terrain                         = false;

            /**
             * Whether what was hit is an object the level placed, which a
             * level edit can find again: a brush or vegetation instance by
             * `model` and `levelObjectAt`, an entity by `entityGuid`.
             */
            bool levelObject = false;
            /** Where the level put it, how it turned it and how it sized it -- even while an edit has moved it. */
            SDK::Vec3 levelObjectAt {};
            SDK::Quat levelObjectRotation {0.0F, 0.0F, 0.0F, 1.0F};
            SDK::Vec3 levelObjectScale {1.0F, 1.0F, 1.0F};
        };

        struct NearbyEntity {
            SDK::Vec3 position {};
            float distance             = 0.0F;
            SDK::EntityId entityId     = 0;
            SDK::EntityGUID entityGuid = 0;
            std::string name;
            std::string entityClass;
            bool physicalized = false;
        };

        struct RayRequest {
            SDK::Vec3 origin {};
            SDK::Vec3 target {};
            KCDC::Shared::RPC::WorldRayMode mode = KCDC::Shared::RPC::WorldRayMode::Cover;
            /** Each hit is a separate solid one, found by re-tracing past the one before. */
            int maxHits            = 1;
            bool ignoreLocalPlayer = true;
        };

        struct ProximityRequest {
            SDK::Vec3 centre {};
            float radius = 0.0F;
            /** An engine class name, or empty for every class. */
            std::string entityClass;
            int maxEntities   = static_cast<int>(KCDC::Shared::RPC::kMaxWorldQueryEntities);
            bool physicalOnly = false;
        };

        /** Registers the server's query RPC; the handler reaches this service back through the application. */
        static void Install();

        /** Answers whatever the server has asked since the last frame. */
        void Update() override;

        /** Drops queries left over from a session that has ended; they time out server-side. */
        void Reset() override;

        /**
         * Appends what the trace meets to `hits`, nearest first. False only
         * when there is no physical world; an empty `hits` is the ordinary
         * "nothing there".
         */
        bool Raycast(const RayRequest &request, std::vector<RayHit> &hits) const;

        /** Probes from `probeUp` above `position` down to `probeDown` below, so a point already slightly underground still resolves. */
        bool ResolveGround(const SDK::Vec3 &position, float probeUp, float probeDown, RayHit &ground) const;

        /** Nearest first. The sphere is filtered out of the box the grid answers with. */
        bool EntitiesInRadius(const ProximityRequest &request, std::vector<NearbyEntity> &entities) const;

        /**
         * What a contact on `collider` at `point` is, told as a trace's hit
         * tells it: terrain, an entity, a vegetation instance or a brush, with
         * its mesh, the material drawn there, the surface type and the level
         * object it is. For a contact no ray found, such as a weapon's. A null
         * collider is terrain. `subMaterial` is the triangle's own material
         * slot, -1 when unknown.
         */
        RayHit DescribeContact(SDK::IPhysicalEntity *collider, const SDK::Vec3 &point, const SDK::Vec3 &normal, std::int16_t surfaceIndex, std::int16_t subMaterial, bool terrain) const;

        /** A hit as the server is told it: everything but this machine's own entity id. */
        static KCDC::Shared::RPC::WorldRayHit ToWire(const RayHit &hit);

      private:
        void QueueQuery(const KCDC::Shared::RPC::WorldQuery &query);
        void AnswerQueries();

        void Answer(const KCDC::Shared::RPC::WorldQuery &query, KCDC::Shared::RPC::WorldQueryAnswer &answer) const;

        std::mutex _incomingMutex;
        std::vector<KCDC::Shared::RPC::WorldQuery> _incomingQueries;
        std::atomic_bool _acceptIncoming {false};
    };
} // namespace KCDC::Features::World
