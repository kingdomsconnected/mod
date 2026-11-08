#pragma once

#include "shared/features/marker/marker_entity.h"

#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

namespace KCDC::Features::Marker {
    /**
     * Client binding between a replicated marker and whatever the engine shows
     * for it.
     *
     * Nothing here is ever reported back. The replica holds a handle from
     * `MarkerService` and keeps it agreeing with the wire: rebuilt when the
     * material or the shape changes, since neither can be restated on a live
     * decal node or render slot, and moved when anything else does.
     *
     * `MarkerService` owns the tick and the level change.
     */
    class Marker final: public Shared::Entities::MarkerEntity {
      public:
        /** Queues teardown: a replica can be destroyed from the peer pump, where neither the 3D engine nor the entity system is safe to touch. */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // Both raised from the peer pump, so both only set the flag the tick reads.
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;

        /** Builds, rebuilds, places and animates this marker. Game thread only. */
        void Frame();

        /** Forgets what the engine was showing without touching it, for a world that is gone. */
        void Unbind();


      private:
        bool TryBuild();

        /** Whether this marker has motion of its own to play out every frame. */
        bool IsAnimated() const {
            return Shared::Entities::IsMeshShape(GetShape()) && (spin != 0.0F || bob > 0.0F);
        }

        /** How far this marker's own motion has carried it this frame. */
        void AnimationOffsets(float &outBobHeight, float &outSpinDegrees);

        std::uint32_t _handle = 0;

        // What the live marker was built from. Only these two force a
        // rebuild; the rest are restated in place.
        std::uint16_t _builtMaterialIndex = Shared::Entities::MarkerEntity::kNoMaterial;
        std::uint8_t _builtShape          = 0xFFU;

        // The area is hung on the carrier when the marker is built, so turning
        // triggering on or off is a rebuild like the other two.
        bool _builtTrigger          = false;
        std::uint32_t _builtColor   = 0;

        glm::vec3 _appliedPosition = glm::vec3(0.0F);
        glm::quat _appliedRotation = glm::identity<glm::quat>();
        float _appliedSize         = 0.0F;
        float _appliedHeight       = 0.0F;

        // Set from the peer pump, cleared on the game thread once the engine
        // matches the wire.
        std::atomic<bool> _dirty {true};


        // A world that is not up yet is held and retried; a catalog row this
        // build does not have is refused. Both are logged once.
        MafiaNet::Time _nextBuildAttempt = 0;
        bool _buildDeferred              = false;
        bool _buildRefused               = false;
    };
} // namespace KCDC::Features::Marker
