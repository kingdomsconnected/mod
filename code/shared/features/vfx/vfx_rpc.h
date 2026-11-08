#pragma once

#include "shared/features/vfx/vfx_entity.h"
#include "shared/features/vfx/vfx_tuning.h"
#include "shared/game/vfx_catalog.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::RPC {
    /**
     * Server -> client: play a particle effect once, near here, now.
     *
     * A blood spray, a spark, a puff of dust: something that happens, is seen
     * by whoever is standing there, and is never asked about again. It carries
     * no identity and leaves no state on either side, which is what separates
     * it from `VfxEntity` -- a burst nobody was near to see simply did not
     * happen, which is the right answer for a hit spark and the wrong one for a
     * campfire.
     *
     * The server sends it only to the players who could see it: same virtual
     * world, and inside the distance the engine would have culled the effect
     * at, which the catalog carries per effect.
     *
     * `attachNetworkId` names a replicated entity to hang the effect on instead
     * of the world; `position` and `rotation` are then an offset in that
     * entity's space. A receiver that cannot resolve the id drops the burst
     * rather than playing it at the world origin.
     */
    struct VfxBurst {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VfxBurst");

        /** Past this a "burst" is something that should have been an entity. */
        static constexpr std::uint32_t kMaxDurationMs = 60000;

        /** How far an offset from an attached entity may reach, in metres. */
        static constexpr float kMaxAttachOffset = 8.0F;

        /** Zero anchors the burst in the world. */
        std::uint64_t attachNetworkId = 0;

        Shared::Game::VfxTuning tuning;

        /** World position, or the offset from `attachNetworkId` when that is set. */
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        /** How long the emitter is kept before the client frees it. Never 0: most effects are continuous and would otherwise never stop. */
        std::uint32_t durationMs = 1000;

        Shared::Game::VfxEffectRef effect;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(attachNetworkId);
            tuning.SerializeFields(fields);
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);
            fields.Field(rotation.x);
            fields.Field(rotation.y);
            fields.Field(rotation.z);
            fields.Field(rotation.w);
            fields.Field(durationMs);
            effect.Serialize(fields);
            _complete = fields.Good();
        }

        bool IsSane() const {
            if (!_complete || !tuning.IsSane() || !effect.Entry()) {
                return false;
            }
            if (durationMs == 0 || durationMs > kMaxDurationMs) {
                return false;
            }
            if (!Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
                return false;
            }
            if (attachNetworkId != 0) {
                return glm::dot(position, position) <= kMaxAttachOffset * kMaxAttachOffset;
            }
            return position.x >= Shared::Entities::HumanEntity::kWorldMin && position.x <= Shared::Entities::HumanEntity::kWorldMax && position.y >= Shared::Entities::HumanEntity::kWorldMin && position.y <= Shared::Entities::HumanEntity::kWorldMax
                   && position.z >= Shared::Entities::HumanEntity::kWorldMin && position.z <= Shared::Entities::HumanEntity::kWorldMax;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
