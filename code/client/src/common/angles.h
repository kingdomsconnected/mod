#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

// The angle conventions a body's facing is measured in. Both of these are
// about the engine's axes rather than about any one module, and both had been
// copied verbatim into the two files that needed them.
namespace KCDC::Common {
    /** Signed angle wrapped to (-pi, pi]. */
    inline float WrapPi(float radians) {
        return std::remainderf(radians, 6.2831853F);
    }

    /** Z-axis yaw in the engine's convention: forward is zero, left positive. */
    inline float YawOf(const glm::quat &facing) {
        const glm::vec3 forward = facing * glm::vec3(0.0F, 1.0F, 0.0F);
        return std::atan2(-forward.x, forward.y);
    }
} // namespace KCDC::Common
