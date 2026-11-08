#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KCDC::Features::World {
    inline float NormalizeHour(float hour) {
        return std::isfinite(hour) ? std::fmod(std::max(hour, 0.0F), 24.0F) : 0.0F;
    }

    inline std::int64_t BlendGameMilliseconds(int seconds) {
        return static_cast<std::int64_t>(std::clamp(seconds, 0, 3600)) * 1000;
    }
} // namespace KCDC::Features::World
