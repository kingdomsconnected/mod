#pragma once

#include "sdk/types.h"

#include <cmath>

namespace KCDC::Common::Spawn {
    struct WorldPlacement {
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
    };

    /** Reject invalid physics inputs and normalize rotation before spawning. */
    inline bool NormalizeSpawnTransform(WorldPlacement &placement, float scale = 1.0F) {
        const auto &position = placement.position;
        auto &rotation       = placement.rotation;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(scale) || scale <= 0.0F) {
            return false;
        }
        if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
            return false;
        }

        // Double precision avoids overflow for finite float components.
        const double lengthSquared = static_cast<double>(rotation.x) * rotation.x + static_cast<double>(rotation.y) * rotation.y + static_cast<double>(rotation.z) * rotation.z + static_cast<double>(rotation.w) * rotation.w;
        if (lengthSquared < 1e-12) {
            return false;
        }
        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        rotation.x                 = static_cast<float>(rotation.x * inverseLength);
        rotation.y                 = static_cast<float>(rotation.y * inverseLength);
        rotation.z                 = static_cast<float>(rotation.z * inverseLength);
        rotation.w                 = static_cast<float>(rotation.w * inverseLength);
        return true;
    }
} // namespace KCDC::Common::Spawn
