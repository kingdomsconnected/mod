#pragma once

#include "../../cryengine/3dengine/i_time_of_day_update_callback.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    // Native world clock; game-thread writes. PublishTimeOfDay drives the sky.
    // Layout: ctor 0x1818424B4; callback vtable 0x183C29488, Update at slot 4.
    class C_Calendar final: public SDK::ITimeOfDayUpdateCallback {
      public:
        static constexpr std::int64_t kMillisecondsPerSecond = 1000;
        static constexpr std::int64_t kMillisecondsPerMinute = 60 * kMillisecondsPerSecond;
        static constexpr std::int64_t kMillisecondsPerHour   = 60 * kMillisecondsPerMinute;
        static constexpr std::int64_t kMillisecondsPerDay    = 24 * kMillisecondsPerHour;

        static C_Calendar *GetInstance();

        std::int64_t GetWorldTimeMilliseconds() const {
            return m_worldTimeMilliseconds;
        }

        std::int64_t GetGameTimeMilliseconds() const {
            return m_gameTimeMilliseconds;
        }

        std::int64_t GetWorldDay() const {
            return m_worldTimeMilliseconds / kMillisecondsPerDay;
        }

        std::int64_t GetWorldDayOfWeek() const {
            return GetWorldDay() % 7;
        }

        float GetHourOfDay() const {
            return static_cast<float>(m_worldTimeMilliseconds % kMillisecondsPerDay) / static_cast<float>(kMillisecondsPerHour);
        }

        // Game milliseconds per real millisecond.
        float GetWorldTimeRatio() const {
            return m_worldTimeRatio;
        }

        bool IsWorldTimePaused() const {
            return m_worldTimePauseHolds.count != 0;
        }

        bool IsTimeOfDayFaked() const {
            return m_fakeTimeOfDay != 0;
        }

        float GetFakeTimeOfDayHour() const {
            return m_fakeTimeOfDayHour;
        }

        // Native setter ignores values earlier than the current world time.
        void SetWorldTimeMilliseconds(std::int64_t milliseconds);

        // Allows earlier epochs; preserves game-time counters and pause holds.
        void AdoptWorldTimeMilliseconds(std::int64_t milliseconds);

        // Advances to the next occurrence of hour and returns the target time.
        std::int64_t SetHourOfDay(float hour);

        // Also updates ITimeOfDay::SAdvancedInfo::animationSpeed.
        void SetWorldTimeRatio(float ratio);

        void PublishTimeOfDay(bool force);

        // Sky override only; Calendar time keeps advancing.
        void SetFakeTimeOfDay(float hour);
        void ClearFakeTimeOfDay();

      private:
        // Native signal storage; subscribers remain game-owned.
        struct CounterListLayout {
            void *m_pHead;
            std::int16_t m_state0008;
            std::int16_t m_state000A;
            std::byte m_padding000C[0x4];
        };

        // MSVC std::map header.
        struct MapLayout {
            void *head;
            std::size_t count;
        };

        CounterListLayout m_gameTimeCounters;          // 0x08
        CounterListLayout m_worldTimeCounters;         // 0x18
        CounterListLayout m_absoluteWorldTimeCounters; // 0x28
        CounterListLayout m_dayChangedListeners;       // 0x38
        std::int64_t m_worldTimeMilliseconds;          // 0x48
        std::int64_t m_gameTimeMilliseconds;           // 0x50
        std::int64_t m_state0058;                      // 0x58
        float m_worldTimeRatio;                        // 0x60, ctor default 15.0f
        float m_derivedTimeRatio;                      // 0x64, ctor default 1.0f
        MapLayout m_worldTimePauseHolds;               // 0x68
        MapLayout m_state0058PauseHolds;               // 0x78
        float m_worldTimeAccumulator;                  // 0x88
        float m_gameTimeAccumulator;                   // 0x8C
        float m_state0058Accumulator;                  // 0x90
        std::uint8_t m_fakeTimeOfDay;                  // 0x94
        std::byte m_padding0095[0x3];
        float m_fakeTimeOfDayHour; // 0x98, ctor default 12.0f
        std::byte m_padding009C[0x4];

        static void AssertLayout() {
            static_assert(sizeof(CounterListLayout) == 0x10, "C_Calendar::CounterListLayout size mismatch");
            static_assert(sizeof(MapLayout) == 0x10, "C_Calendar::MapLayout size mismatch");
            static_assert(offsetof(C_Calendar, m_gameTimeCounters) == 0x08);
            static_assert(offsetof(C_Calendar, m_worldTimeCounters) == 0x18);
            static_assert(offsetof(C_Calendar, m_absoluteWorldTimeCounters) == 0x28);
            static_assert(offsetof(C_Calendar, m_dayChangedListeners) == 0x38, "C_Calendar::m_dayChangedListeners offset mismatch");
            static_assert(offsetof(C_Calendar, m_worldTimeMilliseconds) == 0x48, "C_Calendar::m_worldTimeMilliseconds offset mismatch");
            static_assert(offsetof(C_Calendar, m_gameTimeMilliseconds) == 0x50, "C_Calendar::m_gameTimeMilliseconds offset mismatch");
            static_assert(offsetof(C_Calendar, m_state0058) == 0x58);
            static_assert(offsetof(C_Calendar, m_worldTimeRatio) == 0x60, "C_Calendar::m_worldTimeRatio offset mismatch");
            static_assert(offsetof(C_Calendar, m_derivedTimeRatio) == 0x64);
            static_assert(offsetof(C_Calendar, m_worldTimePauseHolds) == 0x68, "C_Calendar::m_worldTimePauseHolds offset mismatch");
            static_assert(offsetof(C_Calendar, m_state0058PauseHolds) == 0x78, "C_Calendar::m_state0058PauseHolds offset mismatch");
            static_assert(offsetof(C_Calendar, m_worldTimeAccumulator) == 0x88, "C_Calendar::m_worldTimeAccumulator offset mismatch");
            static_assert(offsetof(C_Calendar, m_gameTimeAccumulator) == 0x8C);
            static_assert(offsetof(C_Calendar, m_state0058Accumulator) == 0x90);
            static_assert(offsetof(C_Calendar, m_fakeTimeOfDay) == 0x94, "C_Calendar::m_fakeTimeOfDay offset mismatch");
            static_assert(offsetof(C_Calendar, m_fakeTimeOfDayHour) == 0x98, "C_Calendar::m_fakeTimeOfDayHour offset mismatch");
        }
    };

    static_assert(sizeof(C_Calendar) == 0xA0, "C_Calendar size mismatch");
} // namespace SDK::wh::rpgmodule
