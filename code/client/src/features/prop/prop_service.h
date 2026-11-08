#pragma once

#include "core/feature_registry.h"

#include "prop.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * The client half of prop replication.
     *
     * A prop is the one replicated thing this mod builds outright rather than
     * finding, so there is nothing to scan for: the tick, the level change and
     * the motions the server sends rigid bodies are the whole of it.
     */
    class PropService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the server's motions for rigid props; the handler reaches this service back through the application. */
        static void Install();

        /** Builds, rebuilds and places every replicated prop, and hands each the motions sent for it. Game thread only. */
        void Update() override;

        /** Drops motions left over from a session that has ended. */
        void Reset() override;

        /** Takes a motion off the wire. Callable from the peer pump. */
        void QueueMotion(const Shared::RPC::PropMotion &motion);

        /** Takes the entity a destroyed replica left behind. Callable from the peer pump. */
        void QueueDespawn(SDK::EntityId entityId);

        /** Takes the brush and collider a destroyed replica left behind. Callable from the peer pump. */
        void QueueRelease(SDK::IRenderNode *brush, SDK::IPhysicalEntity *collider);

        /**
         * Removes what those replicas built, and drops every binding when the
         * level underneath has changed.
         *
         * Game thread only, and driven from the application's tick rather than
         * the connected state's, because both halves have to run without a
         * session: the last replicas go when it ends, and a level can load
         * with none at all.
         */
        void DrainDespawns();

        /** Loading-thread edge: the native world these were built in is gone. */
        void NativeContextChanged() override;

      private:
        void ObserveNativeContext();

        /** Hands every motion to its replica, holding one whose replica has not arrived yet: a prop spawned and pushed in one server tick is pushed before it is built. */
        void DeliverMotions();

        struct IncomingMotion {
            Shared::RPC::PropMotion motion;
            MafiaNet::Time receivedAt = 0;
        };

        // Filled from the peer pump, drained on the game thread.
        std::mutex _incomingMotionsMutex;
        std::vector<IncomingMotion> _incomingMotions;
        std::atomic_bool _acceptMotions {false};

        // Waiting for their replica, until `Prop::kMotionShelfLifeMs`. Game thread only.
        std::vector<IncomingMotion> _heldMotions;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;

        std::mutex _pendingDespawnsMutex;
        std::vector<SDK::EntityId> _pendingDespawns;

        struct PendingRelease {
            SDK::IRenderNode *brush            = nullptr;
            SDK::IPhysicalEntity *collider     = nullptr;
        };

        // Under the same lock, and dropped on the same level change: a node
        // queued before it points into an octree that is gone, and a body
        // into a physical world that is.
        std::vector<PendingRelease> _pendingReleases;
    };
} // namespace KCDC::Features::Prop
