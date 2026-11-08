#pragma once

#include "sdk/cryengine/3dengine/i_time_of_day.h"
#include "sdk/types.h"
#include "sdk/wh/environmentmodule/c_weather_system.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::World {
    // Console variables bind directly to these controller members.
    struct RainField {
        const char *label;
        const char *consoleName;
        float SDK::wh::environmentmodule::RainControllerLayout::*member;
        float minimum;
        float maximum;
    };

    struct RainToggle {
        const char *label;
        const char *consoleName;
        std::int32_t SDK::wh::environmentmodule::RainControllerLayout::*member;
    };

    extern const RainField kRainFields[];
    extern const std::size_t kRainFieldCount;
    extern const RainToggle kRainToggles[];
    extern const std::size_t kRainToggleCount;

    /** Where the rain-intensity override sits in `kRainFields`, so no caller spells the index. */
    std::size_t RainIntensityOverrideField();

    struct CVarEntry {
        const char *name;
        const char *label;
        float minimum;
        float maximum;
        bool isToggle;
    };

    extern const CVarEntry kEnvironmentCVars[];
    extern const std::size_t kEnvironmentCVarCount;

    struct RainParams {
        SDK::Vec3 worldPosition {};
        float amount              = 0.0F;
        float currentAmount       = 0.0F;
        float radius              = 0.0F;
        float diffuseDarkening    = 0.0F;
        float dropsAmount         = 0.0F;
        float dropsSpeed          = 0.0F;
        float dropsLighting       = 0.0F;
        float mistAmount          = 0.0F;
        float mistHeight          = 0.0F;
        float puddlesAmount       = 0.0F;
        float puddlesMaskAmount   = 0.0F;
        float puddlesRippleAmount = 0.0F;
        float splashesAmount      = 0.0F;
        bool ignoreVisAreas       = false;
        bool disableOcclusion     = false;
    };

    // NaN bounds mean the profile leaves the condition unset.
    struct ConditionRange {
        float minimum = 0.0F;
        float maximum = 0.0F;
    };

    struct WeatherProfile {
        std::string name;
        ConditionRange temperature {};
        ConditionRange rainIntensity {};
        ConditionRange windVelocity {};
        std::size_t timeOfDayIntervals = 0;
    };

    // Sampled on the game thread; copied to the render thread.
    struct EnvironmentState {
        // Gameplay clock.
        bool calendarAvailable  = false;
        std::int64_t worldDay   = 0;
        std::int64_t dayOfWeek  = 0;
        float hourOfDay         = 0.0F;
        float worldTimeRatio    = 0.0F;
        bool worldTimePaused    = false;
        bool timeOfDayFaked     = false;
        float fakeTimeOfDayHour = 0.0F;

        // Renderer clock.
        bool timeOfDayAvailable = false;
        float time              = 0.0F; // hours
        float sunLatitude       = 0.0F;
        float sunLongitude      = 0.0F;
        bool paused             = false;
        bool sunLinkedToTime    = false;
        SDK::ITimeOfDay::SAdvancedInfo advanced {};

        // Loaded time-of-day presets.
        std::vector<std::string> presetNames;
        std::uint32_t currentPreset     = 0xFFFFFFFFu;
        std::uint32_t blendTargetPreset = 0xFFFFFFFFu; // -1 when idle
        float presetBlendFactor         = 0.0F;

        bool weatherAvailable       = false;
        bool weatherUpdateEnabled   = false;
        float rainIntensity         = 0.0F;
        float rainAmount            = 0.0F; // renderer volume amount, not intensity
        float rainIntensityOverride = 0.0F; // -1 disables the override
        float windSpeed             = 0.0F;

        // Indexed by kRainFields and kRainToggles.
        std::vector<float> rainFieldValues;
        std::vector<std::int32_t> rainToggleValues;

        // True only when rain was submitted in this main frame.
        bool rainParamsAvailable = false;
        RainParams rainParams {};

        // Indexed by kEnvironmentCVars.
        std::vector<float> cvarValues;
        std::vector<char> cvarRegistered;

        std::uint32_t processedCommands = 0;
        std::uint32_t pendingCommands   = 0;
    };

    // Queue methods are thread-safe; Update executes on the game thread.
    class WorldEnvironmentService final {
      public:
        // Earlier hours advance to tomorrow; world time cannot rewind.
        void QueueSetHourOfDay(float hour);

        /**
         * The same hour today, earlier ones included, by seeding the clock
         * past the native setter's forward-only check. Only the offline
         * builder asks for this: nothing schedules off a clock no session
         * has.
         */
        void QueueAdoptHourOfDay(float hour);

        void QueueSkipHours(float hours);

        // Game milliseconds per real millisecond; default 15.
        void QueueSetWorldTimeRatio(float ratio);

        // Pins lighting without advancing the world clock.
        void QueueSetFakeTimeOfDay(float hour);
        void QueueClearFakeTimeOfDay();

        void QueueSetSunPos(float longitude, float latitude);

        void QueueSetSunLinked(bool linked, float longitude, float latitude);

        // CTimeOfDay preset name and game-time seconds.
        void QueueBlendPreset(const char *preset, int blendSeconds, bool force);

        void QueueForceWeatherUpdate();
        void QueueRebuildClouds();

        // Disabling updates also stops rain submission and blending.
        void QueueSetWeatherUpdateEnabled(bool enabled);

        void QueueSetRainField(std::size_t index, float value);
        void QueueSetRainToggle(std::size_t index, std::int32_t value);

        // Nonnegative chooses the rain by hand; negative restores the
        // weather system's own calculation.
        void QueueSetRainOverride(float intensity);

        void QueueSetCVar(const char *name, float value);
        void QueueRefreshProfiles();

        // Game thread only; driven from CCryAction::PostUpdate.
        void Update();

        EnvironmentState GetState() const;
        std::vector<WeatherProfile> GetProfiles() const;
        std::string GetLastAction() const;

      private:
        enum class CommandKind : std::uint8_t {
            SetHourOfDay,
            AdoptHourOfDay,
            SkipHours,
            SetWorldTimeRatio,
            SetFakeTimeOfDay,
            ClearFakeTimeOfDay,
            SetSunPos,
            SetSunLinked,
            BlendPreset,
            ForceWeatherUpdate,
            RebuildClouds,
            SetWeatherUpdateEnabled,
            SetRainField,
            SetRainToggle,
            SetCVar,
        };

        struct Command {
            CommandKind kind = CommandKind::SetHourOfDay;
            std::string text; // preset name, or the variable name
            float value       = 0.0F;
            float value2      = 0.0F;
            int intValue      = 0;
            std::size_t index = 0;
            bool flag         = false;
        };

        void Push(Command command);
        void Execute(const Command &command);
        void RefreshState();
        void RefreshProfiles();
        void SetLastAction(std::string action);

        // Native dispatch borrows these strings; keep their addresses stable.
        std::set<std::string> _appliedPresets;

        mutable std::mutex _mutex;
        std::vector<Command> _pending;
        EnvironmentState _state;
        std::vector<WeatherProfile> _profiles;
        std::string _lastAction;
        std::uint32_t _processedCommands = 0;
        bool _profileRefreshRequested    = true;
    };
} // namespace KCDC::Features::World
