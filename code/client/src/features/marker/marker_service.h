#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/entitysystem/entity_event_listener_with_cleanup.h"
#include "sdk/types.h"

#include "shared/features/marker/marker_shape.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Marker {
    /**
     * Every marker this machine is showing, of either kind.
     *
     * A `Decal` marker is a decal render node, created through
     * `I3DEngine::CreateRenderNode` and owned until `DeleteRenderNode`. That
     * ownership is why this is not `I3DEngine::CreateDecal`, which hands its
     * decal to the engine's decal manager -- a fixed pool that reuses its
     * oldest entry, so blood and arrow impacts would delete a zone, and a
     * client with `e_DecalsAllowGameDecals` at zero would never see one.
     *
     * Every other shape is an editor primitive in a render slot of a carrier
     * entity this service owns. The `_noproxy` variants carry no physics proxy,
     * so a marker is walked through rather than into.
     *
     * A marker whose `trigger` is set also carries an `IEntityAreaProxy`
     * sphere. The entity system tests bodies against it and raises the
     * crossing on the entity that owns the area, which is why this service is
     * a listener rather than something that polls. A projected marker owns no
     * entity, so one is spawned for it when it becomes a trigger.
     *
     * Game thread only, except `NativeContextChanged` and `QueueRemove`.
     */
    class MarkerService final: public KCDC::Core::IFeatureService, public SDK::IEntityEventListener {
      public:
        /** Never handed out, so it is the "no marker" answer everywhere. */
        static constexpr std::uint32_t kNoHandle = 0;

        /** A frame-time bound: a resource that leaks markers should stop working rather than take the session down. */
        static constexpr std::size_t kMaxLiveMarkers = 128;

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /** The entity system's area callback. Game thread, from inside the entity update. */
        void OnEntityEvent(SDK::IEntity *entity, SDK::SEntityEvent *event) override;

        // --- The native seam. Game thread only. ---

        /**
         * Builds a marker at a world pose. `size` is the width in metres and
         * `height` the vertical extent, which only some shapes read.
         * `networkId` is what an area crossing is reported against. Returns
         * `kNoHandle` when the world, the row or the budget refused it.
         */
        std::uint32_t Place(std::uint64_t networkId, std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, std::uint32_t color, bool trigger);

        /**
         * Restates an existing marker's pose and extent. Called every frame for
         * one that spins or bobs, so a mesh writes its slot scale only when the
         * extent actually changed.
         */
        bool Move(std::uint32_t handle, const glm::vec3 &position, const glm::quat &rotation, float size, float height);

        /**
         * Plays a mesh marker's own motion inside its slot, leaving the entity
         * where `Move` put it.
         *
         * Not the entity transform: a moved entity raises XFORM, which an area
         * answers with a full recompute -- every frame, for a spin that cannot
         * move a sphere at all. It also keeps the trigger volume where the
         * server put it rather than bobbing with the visual.
         */
        bool Animate(std::uint32_t handle, float bobHeight, float spinDegrees, float size, float height);

        /** Takes down what `handle` built. False for a handle that has already gone. */
        bool Remove(std::uint32_t handle);

        /** Callable from the peer pump: removes `handle` on the next tick instead of here. */
        void QueueRemove(std::uint32_t handle);

        void RemoveAll();
        bool IsAlive(std::uint32_t handle) const;

      private:
        struct Marker {
            std::uint32_t handle = kNoHandle;

            /** The network id a crossing is reported against. */
            std::uint64_t networkId = 0;

            /** Set for a projected marker; null for a mesh one. */
            SDK::IRenderNode *node = nullptr;

            // Set when this marker owns a carrier entity: a mesh marker always
            // does, and a projected one does once it becomes a trigger.
            SDK::EntityId entityId = 0;

            /** The slot the primitive was loaded into, or -1 for a carrier that holds only an area. */
            int slot = -1;

            // What the slot scale was last written with, so a marker that never
            // resizes is never written to while it spins.
            float appliedSize   = 0.0F;
            float appliedHeight = 0.0F;

            /** Whether this service is registered for area events on `entityId`. */
            bool listening = false;
        };

        /** Whether another marker fits, logging the refusal once per session. */
        bool HasBudget();

        std::uint32_t PlaceDecal(std::uint64_t networkId, std::size_t materialIndex, const glm::vec3 &position, const glm::quat &rotation, float size, std::uint32_t color);
        std::uint32_t PlaceMesh(std::uint64_t networkId, std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, std::uint32_t color);

        /**
         * The catalog row, tinted, or the row as it ships when `color` is 0.
         *
         * Clones are cached per row and colour rather than made per marker: a
         * clone is a material the engine keeps for the level, and a field of
         * fifty markers in one colour has no business making fifty of them.
         */
        SDK::IMaterial *TintedMaterial(std::size_t materialIndex, std::uint32_t color);

        /** Records a built marker and hands back its handle. */
        std::uint32_t Remember(const Marker &built);

        /** Gives `marker` an area sphere and starts listening on it. */
        void AttachTrigger(Marker &marker, const glm::vec3 &position, const glm::quat &rotation, float size, float height);

        /** Takes the marker apart in the engine. Does not touch `_markers`. */
        void Release(const Marker &marker) const;

        Marker *Find(std::uint32_t handle);
        const Marker *Find(std::uint32_t handle) const;

        void ObserveNativeContext();
        void DrainRemovals();

        // Keyed by handle: every replica asks for its own once or twice a
        // frame, so a scan here is quadratic in what is on screen.
        std::unordered_map<std::uint32_t, Marker> _markers;
        std::uint32_t _nextHandle = 1;

        // Keyed by row and colour together. Dropped on a level change with
        // everything else the old world owned.
        std::unordered_map<std::uint64_t, SDK::IMaterial *> _tinted;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;

        // Replicas are destroyed from the peer pump; the teardown happens on the
        // game thread, where neither the 3D engine nor the entity system is
        // part-way through its own work.
        mutable std::mutex _pendingRemovalsMutex;
        std::vector<std::uint32_t> _pendingRemovals;

        bool _warnedAtBudget = false;
    };
} // namespace KCDC::Features::Marker
