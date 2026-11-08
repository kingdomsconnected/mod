#pragma once

#include "shared/features/vfx/vfx_entity.h"

#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

namespace KCDC::Features::Vfx {
    /**
     * Client binding between a replicated particle effect and the emitter it
     * stands for.
     *
     * Nothing about one of these is ever reported back. The replica holds a
     * handle from `VfxService` and keeps it agreeing with the wire: rebuilt
     * when the effect or any spawn parameter changes, since the engine reads
     * those once, and moved when the pose does. The emitter has no duration of
     * its own -- it lasts exactly as long as the replica.
     *
     * `VfxService` owns the registration, the tick and the level change.
     */
    class Vfx final: public Shared::Entities::VfxEntity {
      public:
        /**
         * Queues the emitter for release rather than releasing it: a replica
         * can be destroyed from the loading-time peer pump, where the entity
         * system is part-way through its own work.
         */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // Both raised from the peer pump, so both only set the flag the tick reads.
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;

        /** Builds, rebuilds and places this effect. Game thread only. */
        void Frame();

        /** Forgets the emitter without touching it, for a world that is gone. */
        void Unbind();

      private:
        bool TryBuild();

        bool NeedsRebuild() const {
            return !(_builtEffect == effect) || !(_builtTuning == tuning);
        }

        std::uint32_t _handle = 0;

        // What the running emitter was built from, so a change on the wire is a
        // comparison rather than a flag every writer has to remember to set.
        Shared::Game::VfxEffectRef _builtEffect;
        Shared::Game::VfxTuning _builtTuning;

        glm::vec3 _appliedPosition = glm::vec3(0.0F);
        glm::quat _appliedRotation = glm::identity<glm::quat>();

        // Set from the peer pump, cleared on the game thread once the emitter
        // matches the wire.
        std::atomic<bool> _dirty {true};

        // A world that is not up yet is held and retried; a catalog row this
        // build does not have is refused. Both are logged once.
        MafiaNet::Time _nextBuildAttempt = 0;
        bool _buildDeferred              = false;
        bool _buildRefused               = false;
    };
} // namespace KCDC::Features::Vfx
