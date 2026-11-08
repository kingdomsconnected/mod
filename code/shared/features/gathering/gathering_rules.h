#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KCDC::Shared::Gathering {
    /** The server indexes picked plants, and tells players about them, in square cells this wide. */
    inline constexpr float kSyncCellSize = 64.0F;
    /** The survival level the harvest radius stops growing at. */
    inline constexpr float kMaximumSurvivalLevel = 30.0F;

    /** RPGMinigames slots 17/18, 0x182CF4AB0 and 0x182CF4B18. */
    inline float Radius(float survivalLevel) {
        return std::max(0.05F, survivalLevel * 0.04F + 0.4F);
    }
    inline std::uint32_t CalculateYield(float survivalLevel, std::uint32_t sources, std::uint32_t amount) {
        return static_cast<std::uint32_t>(std::sqrt(survivalLevel) * 0.25F * (sources * amount) + 1.0F);
    }
    /** Native query 0x182ADDA1C deliberately ignores the height difference. */
    inline bool InHarvestRadius(float dx, float dy, float radius) {
        return dx * dx + dy * dy <= radius * radius;
    }
    /** A NaN player coordinate must not bypass a distance rejection. */
    inline bool WithinDistance(float x, float y, float z, float targetX, float targetY, float targetZ, float limit) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(targetX) || !std::isfinite(targetY) || !std::isfinite(targetZ) || !std::isfinite(limit) || limit < 0) {
            return false;
        }
        const float dx = x - targetX, dy = y - targetY, dz = z - targetZ;
        return dx * dx + dy * dy + dz * dz <= limit * limit;
    }
} // namespace KCDC::Shared::Gathering
