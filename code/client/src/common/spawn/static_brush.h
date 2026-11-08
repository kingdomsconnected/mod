#pragma once

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"

#include <mutex>
#include <unordered_set>

namespace KCDC::Common::Spawn {
    /**
     * Every brush the mod stands through `StaticBrush`. To the engine they
     * are bare `CBrush` nodes named for their mesh, exactly what a level's
     * own furniture is, so anything looking for the level's own objects --
     * the World Builder's picker, a level edit's key, a trace's
     * `levelObject` -- asks here first and leaves the mod's alone.
     *
     * Written on the game thread, and from the peer pump when a replica hands
     * its brush over for release, hence the lock.
     */
    class StaticBrushRegistry final {
      public:
        static StaticBrushRegistry &Get();

        void Add(const SDK::IRenderNode *node);
        void Remove(const SDK::IRenderNode *node);

        /** Whether `node` is one of the mod's, rather than the level's. */
        bool Owns(const SDK::IRenderNode *node) const;

      private:
        mutable std::mutex _mutex;
        std::unordered_set<const SDK::IRenderNode *> _nodes;
    };

    /**
     * One static mesh standing as a `CBrush` render node, the form the level
     * keeps its own furniture in: no entity record and no script table, drawn
     * and culled by the 3D engine to the same distance as the level's own
     * copy of that mesh.
     *
     * Its collider, when it has one, is a static body this owns rather than
     * the brush. The brush cannot keep one: with `e_OnDemandMaxSize` at its
     * 100000 default every brush is on demand, and
     * `DestroyPhysicalEntitiesInBox` (0x180530DB8) dephysicalizes every brush
     * in a cell that goes idle -- a live test lost 84 of 595 within a minute
     * of arriving -- through the brush's own +0x80. A body the brush does not
     * hold is out of its reach.
     *
     * A replicated prop stands as one, and so does the mesh a level edit puts
     * in place of the level's own. Game thread only, except `Release`.
     */
    class StaticBrush final {
      public:
        StaticBrush()                               = default;
        StaticBrush(const StaticBrush &)            = delete;
        StaticBrush &operator=(const StaticBrush &) = delete;
        StaticBrush(StaticBrush &&other) noexcept;
        StaticBrush &operator=(StaticBrush &&other) noexcept;
        ~StaticBrush() = default;

        enum class BuildResult {
            Built,
            /** No 3D engine to build in yet. */
            NoEngine,
            /** The game would not load the mesh. */
            MeshRefused,
            /** The mesh stands, but it has no physics geometry to collide with. */
            BuiltWithoutCollider,
        };

        /**
         * Loads `model` and stands it at `matrix`, with a collider of its own
         * when `collide`. Anything already standing is destroyed first.
         */
        BuildResult Build(const char *model, const SDK::Matrix34 &matrix, bool collide);

        /**
         * Moves it. `CBrush::SetMatrix` re-sorts the node in its octree; the
         * collider is built again where it now stands, since its geometry was
         * placed by the old matrix. Meant for something that moves rarely.
         */
        void SetMatrix(const SDK::Matrix34 &matrix);

        /** Destroys the collider and the brush. */
        void Destroy();

        /** Forgets both without touching them, for a world that is gone. */
        void Forget();

        /**
         * Hands both natives to the caller and forgets them, for a release
         * that has to wait for the game thread. The collider goes first when
         * they are destroyed: its foreign data points at the brush.
         */
        void Release(SDK::IRenderNode *&brush, SDK::IPhysicalEntity *&collider);

        /** Removes what `Release` handed over: the collider first, since its foreign data points at the brush. Game thread. */
        static void DestroyReleased(SDK::IRenderNode *brush, SDK::IPhysicalEntity *collider);

        bool IsBuilt() const {
            return _brush != nullptr;
        }

        SDK::IRenderNode *GetNode() const {
            return _brush;
        }

        SDK::IPhysicalEntity *GetCollider() const {
            return _collider;
        }

      private:
        /** The collider at `matrix`; false when the mesh has no physics geometry. */
        bool BuildCollider(const SDK::Matrix34 &matrix);
        void DestroyCollider();

        SDK::IRenderNode *_brush = nullptr;

        /** The brush's mesh, held by the brush itself; read to build the collider again. */
        SDK::IStatObj *_mesh = nullptr;

        SDK::IPhysicalEntity *_collider = nullptr;
        bool _collide                   = false;
    };
} // namespace KCDC::Common::Spawn
