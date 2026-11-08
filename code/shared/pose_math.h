#pragma once

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Poses reach this mod off the wire, out of native physics and out of script,
// and all three can produce a NaN. One that reaches SetPosRotScale takes the
// entity's transform with it, so every pose is checked before it is used.
//
// These live in shared code because both peers ask the same questions: the
// server validates what a script hands it, the client validates what arrives.
namespace KCDC::Shared::Game {
    inline bool IsFinite(const glm::vec3 &value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    inline bool IsFinite(const glm::quat &value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    inline bool IsWorldPosition(const glm::vec3 &value) {
        return IsFinite(value) && std::abs(value.x) <= 16384.0F && std::abs(value.y) <= 16384.0F && std::abs(value.z) <= 16384.0F;
    }

    /**
     * Whether a quaternion can actually be normalized into a rotation.
     *
     * Finite is not enough: a zero or near-zero quaternion is finite and
     * `glm::normalize` turns it into NaNs, which is the same disaster one step
     * later. Anything that is about to normalize a rotation off the wire or out
     * of a binding asks this instead.
     */
    inline bool IsUsableRotation(const glm::quat &value) {
        return IsFinite(value) && glm::dot(value, value) >= 1e-8F;
    }
} // namespace KCDC::Shared::Game
