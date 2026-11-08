#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <networking/replication/network_entity.h>

#include <cmath>

namespace KCDC::Shared::Entities::SiegeFields {
    /** Replaces a value that arrived as NaN or infinity with `fallback`. */
    inline void Finite(float &value, float fallback) {
        if (!std::isfinite(value)) {
            value = fallback;
        }
    }

    /** The placement every siege entity replicates: a world position and an orientation. */
    inline void Transform(Framework::Networking::Replication::FieldSerializer &fields, glm::vec3 &position, glm::quat &rotation) {
        fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
        fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
        fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
        fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
    }
} // namespace KCDC::Shared::Entities::SiegeFields

namespace KCDC::Shared::RPC::SiegeWire {
    inline void Vector(Framework::Networking::Replication::FieldSerializer &fields, glm::vec3 &value) {
        fields.Field(value.x);
        fields.Field(value.y);
        fields.Field(value.z);
    }

    inline bool InWorld(const glm::vec3 &value) {
        return Shared::Game::IsFinite(value) && value.x >= Shared::Entities::HumanEntity::kWorldMin && value.x <= Shared::Entities::HumanEntity::kWorldMax && value.y >= Shared::Entities::HumanEntity::kWorldMin && value.y <= Shared::Entities::HumanEntity::kWorldMax
               && value.z >= Shared::Entities::HumanEntity::kWorldMin && value.z <= Shared::Entities::HumanEntity::kWorldMax;
    }
} // namespace KCDC::Shared::RPC::SiegeWire
