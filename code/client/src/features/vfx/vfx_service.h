#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/types.h"

#include "shared/features/vfx/vfx_rpc.h"
#include "shared/features/vfx/vfx_tuning.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mafianet/get_time.h>
#include <mutex>
#include <vector>

namespace KCDC::Features::Vfx {
    /**
     * Every particle effect this machine is playing.
     *
     * An emitter always lives in an entity's render slot and is stopped by
     * freeing that slot, rather than by holding the `IParticleEmitter` the
     * engine hands back: that interface is 73 unproven `IRenderNode` slots
     * deep, while `LoadParticleEmitter` and `FreeSlot` are both pinned to the
     * game's own call sites. An effect standing in the world is then the same
     * thing as one riding a body, with a carrier entity this service owns.
     *
     * 528 of the 588 catalogued effects are continuous, so nothing started
     * here is left without either a duration or an owner.
     *
     * Game thread only, except `Receive` and `NativeContextChanged`.
     */
    class VfxService final: public KCDC::Core::IFeatureService {
      public:
        /** Never handed out, so it is the "no emitter" answer everywhere. */
        static constexpr std::uint32_t kNoHandle = 0;

        /**
         * How many emitters the mod will keep alive at once. A frame-time
         * bound rather than a memory one: past a couple of hundred the
         * particle system is the frame, and a resource that leaks them should
         * stop working rather than take the session down with it.
         */
        static constexpr std::size_t kMaxLiveEmitters = 192;

        /** Registers the burst RPC. Call once at startup. */
        static void Install();

        /** Network thread: queues a relayed burst for the next tick. */
        void Receive(const Shared::RPC::VfxBurst &burst);

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        // --- The native seam. Game thread only. ---

        /** Whether the game resolves this catalog row's name right now. False before a world is loaded. */
        static bool IsAvailable(const Shared::Game::VfxEffectRef &effect);

        /**
         * Starts an effect at a world pose, on a carrier entity this service
         * owns and removes again. `durationMs` of zero runs until something
         * stops it. Returns `kNoHandle` when the world, the row or the budget
         * refused it.
         */
        std::uint32_t Spawn(const Shared::Game::VfxEffectRef &effect, const glm::vec3 &position, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs);

        /**
         * Starts an effect in a free render slot of an engine entity, where it
         * follows that entity. `offset` and `rotation` place it in the
         * entity's own space. The slot is freed when the duration runs out,
         * when `Stop` is called, or when the entity goes.
         */
        std::uint32_t Attach(const Shared::Game::VfxEffectRef &effect, SDK::EntityId entityId, const glm::vec3 &offset, const glm::quat &rotation, const Shared::Game::VfxTuning &tuning, std::uint32_t durationMs);

        /**
         * Frees what `handle` started. False for a handle that has already
         * gone. By default the emitter only stops emitting and what it emitted
         * fades out on its own lifetime -- up to an hour for some effects;
         * `immediate` kills those particles too.
         */
        bool Stop(std::uint32_t handle, bool immediate = false);

        /** Callable from the peer pump: frees `handle` on the next tick instead of here. */
        void QueueStop(std::uint32_t handle);

        /** Places a world-anchored emitter. Refuses an attached one, whose pose is its entity's business. */
        bool Move(std::uint32_t handle, const glm::vec3 &position, const glm::quat &rotation);

        /** Borrowed carrier and particle nodes; either entry may be null. Game thread only. */
        std::array<const SDK::IRenderNode *, 2> RenderNodes(std::uint32_t handle) const;
        void StopAll();
        bool IsAlive(std::uint32_t handle) const;

        std::size_t LiveCount() const {
            return _emitters.size();
        }

      private:
        struct Emitter {
            std::uint32_t handle   = kNoHandle;
            SDK::EntityId entityId = 0;
            int slot               = -1;
            /** The service spawned `entityId` itself and removes it on the way out. */
            bool carrier = false;
            /** Local clock, or 0 for an emitter that runs until it is stopped. */
            MafiaNet::Time expiresAt = 0;
        };

        /** Loads the effect into a free slot of `entityId`, or -1. */
        int LoadInto(SDK::EntityId entityId, const Shared::Game::VfxEffectRef &effect, const Shared::Game::VfxTuning &tuning);

        /** Whether another emitter fits, logging the refusal once per session. */
        bool HasBudget();

        /** Records a started emitter and hands back its handle. */
        std::uint32_t Remember(SDK::EntityId entityId, int slot, bool carrier, std::uint32_t durationMs);

        /** Takes the emitter apart in the engine. Does not touch `_emitters`. */
        static void Release(const Emitter &emitter, bool immediate = false);

        Emitter *Find(std::uint32_t handle);
        const Emitter *Find(std::uint32_t handle) const;

        /**
         * Forgets every emitter after a level change. Also called from the
         * public entry points: outside a session nothing ticks this service,
         * and the map editor places effects there too.
         */
        void ObserveNativeContext();
        void DrainBursts();
        void DrainStops();
        void ReapExpired();

        std::vector<Emitter> _emitters;
        std::uint32_t _nextHandle = 1;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;

        // Bursts arrive on the network thread and replicas are destroyed from
        // the peer pump; both are applied on the game thread, where the entity
        // system is not part-way through its own work.
        mutable std::mutex _pendingBurstsMutex;
        std::vector<Shared::RPC::VfxBurst> _pendingBursts;
        mutable std::mutex _pendingStopsMutex;
        std::vector<std::uint32_t> _pendingStops;

        bool _warnedAtBudget = false;
    };
} // namespace KCDC::Features::Vfx
