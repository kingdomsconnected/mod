#pragma once

#include <mafianet/string.h>
#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    class WorldEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::World";

        static constexpr std::int64_t kWorldHour    = 3600000;
        static constexpr std::int64_t kWorldDay     = 24 * kWorldHour;
        static constexpr std::int64_t kMaxWorldTime = 3650 * kWorldDay;
        static constexpr std::int64_t kMaxDay       = kMaxWorldTime / kWorldDay;
        static constexpr float kMaxWorldScale       = 200.0F;
        static constexpr float kWetThreshold        = 0.05F;
        static constexpr float kWetCreation         = 0.05F;
        static constexpr float kWetDrying           = 0.01F;
        static constexpr float kPuddleCreation      = 0.01F;
        static constexpr float kPuddleDrying        = 0.005F;
        static constexpr float kPuddleDelay         = 10.0F;

        // Metres per second. Capped because the wind drives a physical area
        // and not just the shaders.
        static constexpr float kMaxWindSpeed = 50.0F;

        std::uint64_t generation = 0;
        std::uint64_t sequence   = 0;

        std::uint64_t clockRevision = 1;
        std::int64_t worldTimeMs    = 8 * kWorldHour;
        std::uint64_t realTimeMs    = 0;
        float timeScale             = 15;

        std::uint64_t weatherRevision  = 1;
        std::string sourcePreset       = "cloudless_sunny";
        std::string targetPreset       = "cloudless_sunny";
        std::int64_t weatherStartMs    = 0;
        std::int64_t weatherDurationMs = 0;

        // Its own revision: the wind is set on its own, not with the sky.
        std::uint64_t windRevision = 1;
        glm::vec3 wind             = glm::vec3(0.0F);

        std::uint64_t rainRevision = 1;
        float rainIntensity        = 0;
        float rainAmount           = 1;
        float wetness              = 0;
        float puddles              = 0;
        float rainPhaseSeconds     = 0;

        static bool ValidWorldScale(float scale) {
            return std::isfinite(scale) && scale >= 0 && scale <= kMaxWorldScale;
        }

        static bool ValidUnit(float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }

        static bool ValidWind(const glm::vec3 &value) {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
                return false;
            }

            // Squared rather than a length, so an overflow is refused here.
            const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;

            return std::isfinite(lengthSquared) && lengthSquared <= kMaxWindSpeed * kMaxWindSpeed;
        }

        static bool ValidPreset(const std::string &name) {
            if (name.empty() || name.size() > 127) {
                return false;
            }

            for (const unsigned char c : name) {
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                    return false;
                }
            }

            return true;
        }

        bool IsSane() const {
            const bool validClock   = worldTimeMs >= 0 && worldTimeMs <= kMaxWorldTime && ValidWorldScale(timeScale);
            const bool validWeather = ValidPreset(sourcePreset) && ValidPreset(targetPreset) && weatherStartMs >= 0 && weatherStartMs <= worldTimeMs && weatherDurationMs >= 0 && weatherDurationMs <= 6 * kWorldHour;
            const bool validWetness = std::isfinite(wetness) && wetness >= 0 && wetness <= 3 && ValidUnit(puddles);
            const bool validRain    = validWetness && ValidUnit(rainIntensity) && ValidUnit(rainAmount) && std::isfinite(rainPhaseSeconds) && rainPhaseSeconds >= 0 && rainPhaseSeconds <= 86400;

            return generation != 0 && sequence != 0 && clockRevision != 0 && weatherRevision != 0 && rainRevision != 0 && windRevision != 0 && validClock && validWeather && validRain && ValidWind(wind);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &serializer) override {
            serializer.Field(generation);
            serializer.Field(sequence);

            serializer.Field(clockRevision);
            serializer.Field(worldTimeMs);
            serializer.Field(realTimeMs);
            serializer.Field(timeScale);

            serializer.Field(weatherRevision);
            serializer.Field(sourcePreset);
            serializer.Field(targetPreset);
            serializer.Field(weatherStartMs);
            serializer.Field(weatherDurationMs);

            serializer.Field(windRevision);
            serializer.Field(wind.x);
            serializer.Field(wind.y);
            serializer.Field(wind.z);

            serializer.Field(rainRevision);
            serializer.Field(rainIntensity);
            serializer.Field(rainAmount);
            serializer.Field(wetness);
            serializer.Field(puddles);
            serializer.Field(rainPhaseSeconds);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &) override {}
    };
} // namespace KCDC::Shared::Entities
