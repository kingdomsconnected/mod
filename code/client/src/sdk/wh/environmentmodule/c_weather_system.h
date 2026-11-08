#pragma once

#include "../../types.h"
#include "i_weather_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::environmentmodule {
    class C_WeatherSystem;

    // Copied by the native weather command queue. The name remains borrowed.
    struct BlendTimeOfDayCommand {
        C_WeatherSystem *weather;
        const char *preset;
        std::int64_t durationMs;
        bool force;
        std::byte m_padding0019[7];

      private:
        static void AssertLayout() {
            static_assert(offsetof(BlendTimeOfDayCommand, weather) == 0);
            static_assert(offsetof(BlendTimeOfDayCommand, preset) == 8);
            static_assert(offsetof(BlendTimeOfDayCommand, durationMs) == 0x10);
            static_assert(offsetof(BlendTimeOfDayCommand, force) == 0x18);
        }
    };

    static_assert(sizeof(BlendTimeOfDayCommand) == 0x20);

    /**
     * One entry of the blender's own preset queue.
     *
     * `C_TimeOfDayBlender::MakeRandomEntry` (0x1813478B4) fills one of these
     * -- RCX the blender, RDX the entry, R8D the preset the queue is leaving,
     * or 0xFFFFFFFF when there is none -- by rolling a weighted choice over
     * every preset's `ITimeOfDay::GetPresetWeight`. `EvaluateBlend`
     * (0x1805544E4) then reads it back: `blendMs` is how long the crossfade
     * into `preset` takes, `holdMs` how long that sky stands afterwards, and
     * when both have elapsed the queue asks for another entry.
     *
     * That is the engine choosing the weather by itself, on each machine's
     * own mt19937.
     */
    struct RandomPresetEntry {
        std::uint32_t preset; // 0x00, index into CTimeOfDay's preset table
        std::uint32_t padding0004;
        std::int64_t blendMs; // 0x08
        std::int64_t holdMs;  // 0x10

      private:
        static void AssertLayout() {
            static_assert(offsetof(RandomPresetEntry, preset) == 0x00);
            static_assert(offsetof(RandomPresetEntry, blendMs) == 0x08);
            static_assert(offsetof(RandomPresetEntry, holdMs) == 0x10);
        }
    };

    static_assert(sizeof(RandomPresetEntry) == 0x18, "RandomPresetEntry size mismatch");

    // Embedded at C_WeatherSystem+0x08; queue entries are 0x18 bytes.
    struct TimeOfDayBlenderLayout {
        bool BlendTimeOfDay(const char *preset, std::int64_t durationMs, bool force);
        void ApplyPresetBlend();
        // Laid out by C_TimeOfDayBlender's constructor (0x180E3BCB4): the
        // entry queue, then the generator MakeRandomEntry rolls, seeded 5489.
        std::byte m_entryQueue[0x28];   // 0x00, std::deque<RandomPresetEntry>, built by 0x1803DF440
        std::byte m_random[0x1384];     // 0x28, the engine's std::mt19937, seeded 5489: 624 words twice over and a 32-bit index
        std::int32_t currentEntryIndex; // 0x13AC, -1 while idle
        std::int64_t lastWorldTimeMs;   // 0x13B0
        std::int64_t elapsedMs;         // 0x13B8, the queue's own cursor
        void *pRandomPresetDelayCVar;   // 0x13C0, wh_env_RandomPresetDelay
        void *pDebugTodCVar;            // 0x13C8, wh_env_debugTod

      private:
        static void AssertLayout() {
            static_assert(offsetof(TimeOfDayBlenderLayout, m_random) == 0x28);
            static_assert(offsetof(TimeOfDayBlenderLayout, currentEntryIndex) == 0x13AC);
            static_assert(offsetof(TimeOfDayBlenderLayout, lastWorldTimeMs) == 0x13B0);
            static_assert(offsetof(TimeOfDayBlenderLayout, elapsedMs) == 0x13B8);
            static_assert(offsetof(TimeOfDayBlenderLayout, pRandomPresetDelayCVar) == 0x13C0);
            static_assert(offsetof(TimeOfDayBlenderLayout, pDebugTodCVar) == 0x13C8);
        }
    };

    /** One timed target of the 0x15F8 block: a point, a period and a value. */
    struct WeatherTimedTargetLayout {
        SDK::Vec3 point;            // 0x00, seeded from a process-wide constant
        std::uint16_t periodMs;     // 0x0C, 2000 and 1000 by default
        std::uint16_t padding000E;
        float value;                // 0x10, 1000 and 3000 by default
    };

    static_assert(sizeof(WeatherTimedTargetLayout) == 0x14, "WeatherTimedTargetLayout size mismatch");

    /**
     * The block C_WeatherSystem builds at +0x15F8 with 0x180E3BA40: two
     * lists, two pointer vectors (the first reserved to 32), two tables of
     * ten 0x18-byte entries, a point and two timed targets.
     */
    struct WeatherStateBlockLayout {
        std::byte lists[2][0x10];           // 0x000, two std::list heads
        void *vectorA[3];                   // 0x020, reserved to 32 entries
        void *vectorB[3];                   // 0x038
        std::byte entriesA[10][0x18];       // 0x050
        std::byte entriesB[10][0x18];       // 0x140
        SDK::Vec3 point0230;                // 0x230, seeded from a process-wide constant
        WeatherTimedTargetLayout targets[2]; // 0x23C
        std::uint32_t padding0264;
        std::uint64_t state0268[6];         // 0x268
        std::uint64_t state0298;            // 0x298
        std::uint32_t state02A0;            // 0x2A0
        std::uint32_t padding02A4;
    };

    static_assert(sizeof(WeatherStateBlockLayout) == 0x2A8, "WeatherStateBlockLayout size mismatch");

    // Embedded at +0x13E0; distinct from the C_Rain entity extension.
    struct RainControllerLayout {
        std::int32_t disableRainController;  // 0x00, wh_env_DisableRainController
        std::int32_t rainDebug;              // 0x04, wh_env_RainDebug
        std::int32_t rainLayers;             // 0x08, wh_env_RainLayers
        float rainCurrentAmount;             // 0x0C, wh_env_RainCurrentAmount
        float rainThreshold;                 // 0x10
        float rainFilterFrequency;           // 0x14
        float wetDryThreshold;               // 0x18
        float lightningThreshold;            // 0x1C
        float rainDiffuseDarkening;          // 0x20
        float rainMovementStrength;          // 0x24
        float rainMovementBlend;             // 0x28
        float dirtCreationSpeed;             // 0x2C
        float dirtDryingSpeed;               // 0x30
        float puddleCreationDelay;           // 0x34
        float puddleDryingDelay;             // 0x38
        float puddleCreationSpeed;           // 0x3C
        float puddleDryingSpeed;             // 0x40
        float puddleMaskMinimum;             // 0x44
        float puddleMaskMaximum;             // 0x48
        float state004C;                     // 0x4C
        float state0050;                     // 0x50
        float rainWindStrength;              // 0x54
        float rainDropsAmountMultiplier;     // 0x58
        float rainDropsBaseSpeed;            // 0x5C
        float rainDropsSpeedMultiplier;      // 0x60
        float rainDropsWindSpeedMultiplier;  // 0x64
        float splashesAmountMultiplier;      // 0x68
        float puddleRipplesAmountMultiplier; // 0x6C
        float rainIntensityOverride;         // 0x70
        float cloudDensitySaturationValue;   // 0x74
        C_WeatherSystem *weatherSystem;      // 0x78
        float rainIntensity;                 // 0x80
        std::uint32_t padding0084;
        std::uint64_t state0088[3];          // 0x88, zeroed by the constructor (0x1810A702C)
        SDK::SRainParams params;    // 0xA0, camera-local renderer state
        std::int32_t phase;         // 0x1C0: dry, raining, drying
        float phaseDurationSeconds; // 0x1C4
        float phaseElapsedSeconds;  // 0x1C8
        float rainStartPosition[3]; // 0x1CC, BeginRaining (0x182ADECC0) copies the translation column of the matrix from qword_18492D8C0 slot +0x438
        std::uint64_t state01D8;    // 0x1D8, BeginRaining zeroes
        std::uint32_t state01E0;    // 0x1E0, BeginRaining zeroes
        std::uint32_t state01E4;    // 0x1E4, lightning entity handle; RemoveLightningEntity releases and clears it
        std::byte m_unread01E8[0x08]; // 0x1E8, no writer in the controller's functions

      private:
        static void AssertLayout() {
            static_assert(offsetof(RainControllerLayout, disableRainController) == 0x00);
            static_assert(offsetof(RainControllerLayout, rainDebug) == 0x04);
            static_assert(offsetof(RainControllerLayout, rainLayers) == 0x08);
            static_assert(offsetof(RainControllerLayout, rainCurrentAmount) == 0x0C);
            static_assert(offsetof(RainControllerLayout, rainThreshold) == 0x10);
            static_assert(offsetof(RainControllerLayout, rainFilterFrequency) == 0x14);
            static_assert(offsetof(RainControllerLayout, wetDryThreshold) == 0x18);
            static_assert(offsetof(RainControllerLayout, lightningThreshold) == 0x1C);
            static_assert(offsetof(RainControllerLayout, rainDiffuseDarkening) == 0x20);
            static_assert(offsetof(RainControllerLayout, rainMovementStrength) == 0x24);
            static_assert(offsetof(RainControllerLayout, rainMovementBlend) == 0x28);
            static_assert(offsetof(RainControllerLayout, dirtCreationSpeed) == 0x2C);
            static_assert(offsetof(RainControllerLayout, dirtDryingSpeed) == 0x30);
            static_assert(offsetof(RainControllerLayout, puddleCreationDelay) == 0x34);
            static_assert(offsetof(RainControllerLayout, puddleDryingDelay) == 0x38);
            static_assert(offsetof(RainControllerLayout, puddleCreationSpeed) == 0x3C);
            static_assert(offsetof(RainControllerLayout, puddleDryingSpeed) == 0x40);
            static_assert(offsetof(RainControllerLayout, puddleMaskMinimum) == 0x44);
            static_assert(offsetof(RainControllerLayout, puddleMaskMaximum) == 0x48);
            static_assert(offsetof(RainControllerLayout, state004C) == 0x4C);
            static_assert(offsetof(RainControllerLayout, state0050) == 0x50);
            static_assert(offsetof(RainControllerLayout, rainWindStrength) == 0x54);
            static_assert(offsetof(RainControllerLayout, rainDropsAmountMultiplier) == 0x58);
            static_assert(offsetof(RainControllerLayout, rainDropsBaseSpeed) == 0x5C);
            static_assert(offsetof(RainControllerLayout, rainDropsSpeedMultiplier) == 0x60);
            static_assert(offsetof(RainControllerLayout, rainDropsWindSpeedMultiplier) == 0x64);
            static_assert(offsetof(RainControllerLayout, splashesAmountMultiplier) == 0x68);
            static_assert(offsetof(RainControllerLayout, puddleRipplesAmountMultiplier) == 0x6C);
            static_assert(offsetof(RainControllerLayout, rainIntensityOverride) == 0x70);
            static_assert(offsetof(RainControllerLayout, cloudDensitySaturationValue) == 0x74);
            static_assert(offsetof(RainControllerLayout, weatherSystem) == 0x78);
            static_assert(offsetof(RainControllerLayout, rainIntensity) == 0x80);
            static_assert(offsetof(RainControllerLayout, params) == 0xA0);
            static_assert(offsetof(RainControllerLayout, phase) == 0x1C0);
            static_assert(offsetof(RainControllerLayout, phaseDurationSeconds) == 0x1C4);
            static_assert(offsetof(RainControllerLayout, phaseElapsedSeconds) == 0x1C8);
            static_assert(offsetof(RainControllerLayout, rainStartPosition) == 0x1CC);
            static_assert(offsetof(RainControllerLayout, state01E0) == 0x1E0);
            static_assert(offsetof(RainControllerLayout, state01E4) == 0x1E4);
        }
    };

    struct WeatherLayerProfileControllerLayout {
        C_WeatherSystem *weatherSystem; // 0x00
        void **profilesBegin;           // 0x08
        void **profilesEnd;             // 0x10
        void **profilesCapacity;        // 0x18
        std::uint32_t debugDraw;        // 0x20
        std::byte m_padding0024[0x4];

      private:
        static void AssertLayout() {
            static_assert(offsetof(WeatherLayerProfileControllerLayout, weatherSystem) == 0x00);
            static_assert(offsetof(WeatherLayerProfileControllerLayout, profilesBegin) == 0x08);
            static_assert(offsetof(WeatherLayerProfileControllerLayout, profilesEnd) == 0x10);
            static_assert(offsetof(WeatherLayerProfileControllerLayout, profilesCapacity) == 0x18);
            static_assert(offsetof(WeatherLayerProfileControllerLayout, debugDraw) == 0x20);
        }
    };

    static_assert(sizeof(TimeOfDayBlenderLayout) == 0x13D0, "TimeOfDayBlenderLayout size mismatch");
    static_assert(sizeof(RainControllerLayout) == 0x1F0, "RainControllerLayout size mismatch");
    static_assert(sizeof(WeatherLayerProfileControllerLayout) == 0x28, "WeatherLayerProfileControllerLayout size mismatch");

    // Layout: ctor 0x180E3BBC0; 21-slot vtable at 0x183AB8D20.
    class C_WeatherSystem: public SDK::wh::environmentmodule::IWeatherSystem {
      public:
        ~C_WeatherSystem() override = default;

        static C_WeatherSystem *GetInstance();

        /** The weather system a blender belongs to; one is embedded in each. */
        static C_WeatherSystem *FromTimeOfDayBlender(TimeOfDayBlenderLayout *blender) {
            return reinterpret_cast<C_WeatherSystem *>(reinterpret_cast<std::byte *>(blender) - offsetof(C_WeatherSystem, m_timeOfDayBlender));
        }

        TimeOfDayBlenderLayout *GetTimeOfDayBlender() {
            return &m_timeOfDayBlender;
        }

        const TimeOfDayBlenderLayout *GetTimeOfDayBlender() const {
            return &m_timeOfDayBlender;
        }

        RainControllerLayout *GetRainController() {
            return &m_rainController;
        }

        const RainControllerLayout *GetRainController() const {
            return &m_rainController;
        }

        WeatherLayerProfileControllerLayout *GetWeatherLayerProfileController() {
            return &m_weatherLayerProfiles;
        }

        const WeatherLayerProfileControllerLayout *GetWeatherLayerProfileController() const {
            return &m_weatherLayerProfiles;
        }

        float GetCurrentRainIntensity() const {
            return m_rainController.rainIntensity;
        }

        float GetConfiguredRainAmount() const {
            return m_rainController.rainCurrentAmount;
        }

        float GetState13D8Raw() const {
            return m_state13D8;
        }

        std::uint32_t GetState15C4Raw() const {
            return m_rainController.state01E4;
        }

        float GetState18E0Raw() const {
            return m_state18E0;
        }

        float GetState18E4Raw() const {
            return m_state18E4;
        }

        std::uint32_t GetState18E8Raw() const {
            return m_state18E8;
        }

        bool GetState18ECRaw() const {
            return m_state18EC;
        }

      private:
        TimeOfDayBlenderLayout m_timeOfDayBlender;                  // 0x0008
        float m_state13D8;                                          // 0x13D8
        std::uint32_t m_padding13DC;                                // 0x13DC
        RainControllerLayout m_rainController;                      // 0x13E0
        WeatherLayerProfileControllerLayout m_weatherLayerProfiles; // 0x15D0
        WeatherStateBlockLayout m_stateBlock;                       // 0x15F8
        void *m_pMoonCVar;                                          // 0x18A0, wh_env_moon
        std::uint64_t m_state18A8[4];                               // 0x18A8
        float m_state18C8;                                          // 0x18C8, -1 by default
        std::uint32_t m_state18CC;                                  // 0x18CC
        float m_state18D0[3];                                       // 0x18D0, -1 by default
        std::uint32_t m_state18DC;                                  // 0x18DC
        float m_state18E0;                                          // 0x18E0
        float m_state18E4;                                          // 0x18E4
        std::uint32_t m_state18E8;                                  // 0x18E8
        bool m_state18EC;                                           // 0x18EC
        std::byte m_padding18ED[0x3];                                // 0x18ED
        std::byte m_map18F0[0x40];                                  // 0x18F0, an std::unordered_map built by 0x180BBB344

        static void AssertLayout() {
            static_assert(offsetof(C_WeatherSystem, m_timeOfDayBlender) == 0x08);
            static_assert(offsetof(C_WeatherSystem, m_state13D8) == 0x13D8);
            static_assert(offsetof(C_WeatherSystem, m_rainController) == 0x13E0);
            static_assert(offsetof(C_WeatherSystem, m_weatherLayerProfiles) == 0x15D0);
            static_assert(offsetof(C_WeatherSystem, m_stateBlock) == 0x15F8);
            static_assert(offsetof(C_WeatherSystem, m_pMoonCVar) == 0x18A0);
            static_assert(offsetof(C_WeatherSystem, m_state18C8) == 0x18C8);
            static_assert(offsetof(C_WeatherSystem, m_state18E0) == 0x18E0);
            static_assert(offsetof(C_WeatherSystem, m_state18E4) == 0x18E4);
            static_assert(offsetof(C_WeatherSystem, m_state18E8) == 0x18E8);
            static_assert(offsetof(C_WeatherSystem, m_state18EC) == 0x18EC);
        }
    };

    static_assert(sizeof(C_WeatherSystem) == 0x1930, "C_WeatherSystem size mismatch");
} // namespace SDK::wh::environmentmodule
