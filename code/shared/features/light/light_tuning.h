#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace KCDC::Shared::Game {
    /** Persistent point-light controls shared by editor, scripting and replication. */
    struct LightTuning {
        glm::vec3 color {1.0F, 0.65F, 0.3F};
        float intensity  = 1.0F;
        float radius     = 5.0F;
        bool castShadows = false;

        static constexpr float kMaxIntensity = 100.0F;
        static constexpr float kMinRadius    = 0.1F;
        static constexpr float kMaxRadius    = 100.0F;

        bool operator==(const LightTuning &) const = default;

        bool IsSane() const {
            return InRange(color.x, 0.0F, 1.0F) && InRange(color.y, 0.0F, 1.0F) && InRange(color.z, 0.0F, 1.0F) && InRange(intensity, 0.0F, kMaxIntensity) && InRange(radius, kMinRadius, kMaxRadius);
        }

        template <typename Fields>
        void SerializeFields(Fields &fields) {
            fields.Field(color.x);
            fields.Field(color.y);
            fields.Field(color.z);
            fields.Field(intensity);
            fields.Field(radius);
            fields.Field(castShadows);
        }

      private:
        static bool InRange(float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        }
    };
} // namespace KCDC::Shared::Game
