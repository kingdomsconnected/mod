#pragma once

#include "shared/features/world/world_entity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::World {
    /**
     * The server's clock and weather, published as the one replicated
     * WorldEntity every client reads.
     *
     * Every write is a typed, range-checked verb, so `/world` and the `World`
     * binding are two callers of one implementation. A verb advances the clock
     * before it reads it and publishes on success, because a script can call
     * one between two updates.
     *
     * The clock only moves forward: winding it back would leave every client
     * ahead of the server with no way to agree again, so a verb that would
     * rewind is refused rather than clamped.
     */
    class WorldService final {
      public:
        void Install();
        void Update();

        /** Whole days the clock has run, from the level's own midnight. */
        std::int64_t GetDay() const;

        /** Hour within that day, from 0 up to but not including 24. */
        double GetHour() const;

        float GetTimeScale() const;

        /** The replicated world clock in milliseconds. */
        std::uint64_t GetWorldTimeMilliseconds() const {
            return static_cast<std::uint64_t>(_worldTimeMs);
        }

        /** The preset the sky is blending towards, which is where it settles. */
        const std::string &GetWeather() const;

        /** What it is blending from; the same as GetWeather once settled. */
        const std::string &GetPreviousWeather() const;

        bool IsWeatherBlending() const;

        /** Game-seconds left of the blend, 0 once the sky has settled. */
        double GetWeatherRemaining() const;

        float GetRainIntensity() const;
        float GetRainAmount() const;

        /** The global wind, in metres per second, world space. */
        const glm::vec3 &GetWind() const;

        /** Its length: how hard it is blowing, whichever way. */
        float GetWindSpeed() const;

        /** Derived from the rain, so read-only. */
        float GetWetness() const;
        float GetPuddles() const;

        bool SetTimeScale(float scale);

        /** The next time it is this hour, rolling into tomorrow when it has passed today. */
        bool SetHour(double hour);

        bool SetTime(std::int64_t day, double hour);

        /**
         * Blends to a native time-of-day preset over `seconds` game-seconds.
         * Refused while an earlier blend runs: an unfinished one has no single
         * preset to leave from. Only a client can tell whether a preset
         * exists. Raises `worldWeatherChange`.
         */
        bool SetWeather(const std::string &preset, double seconds);

        /** Crossing the wetness threshold restarts the soak and dry phase. */
        bool SetRain(float intensity, float amount);

        /**
         * Sets the wind every client applies to its own 3D engine. There is
         * nothing to blend against: a gust is a script ramping this itself.
         */
        bool SetWind(const glm::vec3 &wind);

        std::string ExecuteCommand(const std::vector<std::string> &args);

      private:
        void Tick(std::uint64_t now);
        bool AdvanceTo(std::int64_t target);
        void Publish();
        std::string Describe() const;

        /** Raises `worldDayChange` when the clock has crossed midnight, whether it walked over or a verb jumped past. */
        void ReportDayChange(std::int64_t previousDay) const;

        KCDC::Shared::Entities::WorldEntity *_entity = nullptr;
        std::int64_t _worldTimeMs                     = 0;
        std::uint64_t _realTimeMs                     = 0;
        double _fraction                              = 0;
        float _wetness                                = 0;
        float _puddles                                = 0;
        float _rainPhaseSeconds                       = 0;
        std::uint64_t _lastPublish                    = 0;
    };
} // namespace KCDC::Features::World
