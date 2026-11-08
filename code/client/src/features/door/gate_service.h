#pragma once

#include "core/feature_registry.h"

#include "gate.h"

#include "sdk/types.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::Door {
    /**
     * The client half of gate synchronization.
     *
     * Gates are not built by the mod, and the level builds each one twice:
     * a `GeomEntity` holding the animated character, alone in a deferred
     * quest layer the level does not load, and a static `_opened` brush the
     * world shows every other time. The shipped game swaps the first in only
     * for a quest's TrackView sequence.
     *
     * The server decides everything about a gate from the level's own data,
     * so this reports nothing. For every gate it has a replica of, it streams
     * the animated one in, hides the stand-in, and drives it to the pose the
     * server's cycle implies; when the replica goes, both come back as the
     * game had them.
     *
     * Finding the entity is a lookup, not a search: the generated placement
     * table carries each gate's `EntityGUID`, the same key the game's own
     * TrackView sequences bind these entities by.
     *
     * A swapped gate -- the Ratborsch and Nebakov fortress gates, the Ruthard
     * palace gate -- has no entity at all. The level draws it open and shut
     * as two layers of static brushes, so this switches those layers the
     * way the quests do, and gives each back as the game had it when the
     * replica goes.
     */
    class GateService final: public KCDC::Core::IFeatureService {
      public:
        /** This machine's clock in milliseconds, the same base the server stamps. */
        static std::uint64_t Now();

        /** Brings every replicated gate into this world and drives the bound ones. */
        void Update() override;

        /** Drops everything belonging to the session that just ended. */
        void Reset() override;

        /** Loading-thread edge: the native world this was bound to is gone. */
        void NativeContextChanged() override;

      private:
        /** How far a gate has got, so a stall says which stage it is at. */
        enum class Stage : std::uint8_t {
            NoLayer,
            LayerRequested,
            LayerReleased,
            SweepOnly,
            NoCharacter,
            Bound
        };

        /** A gate layer this client streamed in, and what the game had before it did. */
        struct LayerClaim {
            /** `CEntityLayer::IsEnabledBrush` before the first request; what goes back. */
            bool wasEnabled = false;

            /** When it was last asked to stream in, 0 before the first time. */
            std::uint64_t requestedAt = 0;
        };

        /** A brush layer a swapped gate switched, and what the game had before it did. */
        struct SwappedLayer {
            /** `CEntityLayer::IsEnabledBrush` before the first switch; what goes back. */
            bool wasEnabled = false;

            /** What this client last asked of it. */
            bool enabled = false;

            /** When this client last switched it, 0 before the first time. */
            std::uint64_t switchedAt = 0;
        };

        /**
         * Asks the engine to stream a gate's deferred layer in, and
         * remembers how the game had it the first time. The request repeats
         * at most every `kLayerRequestRetryMs`: the read is asynchronous, and
         * asking again meanwhile only restarts it.
         *
         * Never gated on IsLayerEnabled: level load builds layers through
         * AddLayer and never calls Enable, so one that merely exists still
         * carries the constructor's enabled byte and answers true.
         */
        void ClaimLayer(const Shared::Game::GatePlacement &placement, std::uint64_t now);

        /**
         * Gives a claimed layer back as the game had it. Does nothing to a
         * layer this client never claimed: one the game loaded itself stays
         * the game's.
         */
        void ReleaseLayer(const Shared::Game::GatePlacement &placement);

        /** Logs one line when a gate changes stage, and never repeats itself. */
        void NoteStage(SDK::EntityGUID gateGuid, Stage stage, const char *detail);

        /**
         * The live entity for one gate GUID, or null.
         *
         * Tries `FindEntityByGuid`, then, when `allowSweep`, a sweep comparing
         * `GetGuid()`: `CEntitySystem::SpawnEntity` registers a guid mapping
         * only on the branch that allocates a fresh `CEntity`, so an entity
         * served from the pool has none. The GUID on the entity is always
         * right, which is what the sweep relies on.
         */
        static SDK::IEntity *ResolveEntity(SDK::EntityGUID gateGuid, bool allowSweep, bool &viaSweep);

        void ObserveNativeContext(const std::string &level);
        void RefreshReplicaIndex();

        /**
         * Streams in and binds every replicated gate, and gives back the
         * layers of those it no longer has, every frame. Only the entity
         * sweep is rationed, per gate, by `Gate::SweepDue`.
         */
        void ResolveLayers(std::uint64_t now);

        /** States every bound replica's pose, every frame. */
        void DriveReplicas(std::uint64_t now);

        /**
         * Switches every swapped gate's layers to what its replica says, and
         * moves the layers of a gate this client no longer has back to how
         * the game had them.
         */
        void DriveSwappedGates(std::uint64_t now);

        /**
         * Switches one layer toward `enabled` and remembers how the game had
         * it the first time. A layer is held for `kLayerSwitchHoldMs` after
         * each switch: one switched back while its brushes are still
         * streaming in can keep them drawn.
         */
        void SwitchSwappedLayer(const char *layer, bool enabled, std::uint64_t now);

        /** Rebuilt each tick, so it never outlives the replicas in it. */
        std::unordered_map<SDK::EntityGUID, Gate *> _replicaIndex;

        /** Last stage logged per gate, so the log carries edges and not a stream. */
        std::unordered_map<SDK::EntityGUID, Stage> _stages;

        std::unordered_map<SDK::EntityGUID, LayerClaim> _layerClaims;

        /** Every layer a swapped gate has switched, by name. */
        std::unordered_map<std::string, SwappedLayer> _swappedLayers;

        /** What the swapped gates want of their layers this frame; reused so a frame does not allocate. */
        std::vector<std::pair<const char *, bool>> _wantedLayers;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext   = 0;
        std::uint32_t _observedLevelHash = 0;
        std::string _observedLevel;
    };
} // namespace KCDC::Features::Door
