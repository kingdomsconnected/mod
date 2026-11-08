#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/vfx/vfx_effect_ref.h"
#include "shared/features/vfx/vfx_tuning.h"
#include "shared/game/vfx_catalog.h"

#include <algorithm>
#include <cstdint>
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::Entities {
    /**
     * A particle effect the server put in the world and left running.
     *
     * This is the half of the feature the interest grid is for. A campfire has
     * to be there for whoever walks up to it however long after it was placed,
     * which a one-shot message cannot do -- it only reaches the clients that
     * were nearby when it was sent. As a replicated entity it streams in and
     * out like a prop, and ending one is destroying the entity.
     *
     * Everything but the pose is construction state that also rides the delta
     * channel, because the engine reads the effect name and every spawn
     * parameter once, when the emitter is created. A change to any of them is a
     * rebuild on every client that can see it.
     */
    class VfxEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Vfx";

        static constexpr std::uint16_t kNoEffect = 0xFFFFU;

        /**
         * Bounds on the streaming radius the server derives from the effect's
         * own `CameraMaxDistance`.
         *
         * The floor covers the effects that declare nothing, which would
         * otherwise never stream in at all. The ceiling is the argument
         * `PropEntity::kStreamRange` makes: the grid is queried at the widest
         * range any live entity carries, so one effect authored at 1000m would
         * widen that query for every viewer in the session.
         */
        static constexpr float kMinStreamRange = 25.0F;
        static constexpr float kMaxStreamRange = 250.0F;

        /** The streaming radius for one catalog row: what the engine would cull it at, inside the bounds above. */
        static float StreamRangeFor(const Shared::Game::VfxEffectEntry &effect) {
            return std::clamp(effect.viewDistance, kMinStreamRange, kMaxStreamRange);
        }

        /** A row of `Shared::Game::g_VfxEffects`, so no catalog name crosses the wire, or a streamed effect by name. */
        Shared::Game::VfxEffectRef effect;

        Shared::Game::VfxTuning tuning;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeBuild(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeBuild(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
        }

      private:
        /** Everything a receiver has to build the emitter again for. */
        void SerializeBuild(Framework::Networking::Replication::FieldSerializer &fields) {
            effect.Serialize(fields);
            tuning.SerializeFields(fields);
        }
    };
} // namespace KCDC::Shared::Entities
