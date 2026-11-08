#pragma once

#include "sdk/types.h"

#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

// The SDK half of the pose predicates. The glm half lives in
// `shared/pose_math.h`, because both peers ask the same questions of a pose off
// the wire; this adds the engine's own vector types and forwards the rest, so a
// client call site has one overload set for both.
namespace KCDC::Common {
    inline bool IsFinite(const glm::vec3 &value) {
        return Shared::Game::IsFinite(value);
    }

    inline bool IsFinite(const glm::quat &value) {
        return Shared::Game::IsFinite(value);
    }

    inline bool IsFinite(const SDK::Vec3 &value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    inline bool IsFinite(const SDK::Quat &value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    inline bool IsFinite(const SDK::Vec3 &position, const SDK::Quat &rotation) {
        return IsFinite(position) && IsFinite(rotation);
    }
} // namespace KCDC::Common
