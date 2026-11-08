#pragma once

#include "i_time_of_day.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct TimeOfDayClockStateLayout {
        float m_time;         // 0x00, GetTime/SetTime
        float m_sunLatitude;  // 0x04, GetSunLatitude/SetSunPos
        float m_sunLongitude; // 0x08, GetSunLongitude/SetSunPos
        std::uint8_t m_state000C;
        std::uint8_t m_paused;               // 0x0D, SetPaused/Tick
        std::uint8_t m_sunLinkedToTimeOfDay; // 0x0E
        std::byte m_padding000F[0x1]; // alignment, never written by the constructor 0x180DA8758
        ITimeOfDay::SAdvancedInfo m_advancedInfo; // 0x10
        std::byte m_padding0034[0x4]; // alignment before m_pTimer
        void *m_pTimer;    // 0x38
        float m_timeScale; // 0x40, initialized to 1.0f
        std::byte m_padding0044[0x4]; // alignment before m_pTimeOfDaySpeedCVar
        void *m_pTimeOfDaySpeedCVar; // 0x48, e_TimeOfDaySpeed

      private:
        static void AssertLayout() {
            static_assert(offsetof(TimeOfDayClockStateLayout, m_time) == 0x00);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_sunLatitude) == 0x04);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_sunLongitude) == 0x08);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_state000C) == 0x0C);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_paused) == 0x0D);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_sunLinkedToTimeOfDay) == 0x0E);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_advancedInfo) == 0x10);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_pTimer) == 0x38);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_timeScale) == 0x40);
            static_assert(offsetof(TimeOfDayClockStateLayout, m_pTimeOfDaySpeedCVar) == 0x48);
        }
    };

    static_assert(sizeof(TimeOfDayClockStateLayout) == 0x50, "TimeOfDayClockStateLayout size mismatch");

    // 55-slot vtable at 0x183AA5758; deleting destructor confirms size.
    class CTimeOfDay: public ITimeOfDay {
      public:
        ~CTimeOfDay() override = default;

        TimeOfDayClockStateLayout *GetClockState() {
            return &m_clockState;
        }
        const TimeOfDayClockStateLayout *GetClockState() const {
            return &m_clockState;
        }

        std::size_t GetLoadedPresetCount() const {
            return m_presetInfoBegin != nullptr ? static_cast<std::size_t>(m_presetInfoEnd - m_presetInfoBegin) : 0;
        }

        std::uint32_t GetCurrentPreset() const {
            return m_currentPresetIndex;
        }
        std::uint32_t GetBlendTargetPreset() const {
            return m_blendPresetIndex;
        }
        float GetBlendFactor() const {
            return m_blendFactor;
        }

        // Borrowed until the level unloads.
        const char *GetPresetNameAt(std::size_t index) const {
            if (index >= GetLoadedPresetCount()) {
                return nullptr;
            }
            return m_presetInfoBegin[index].name.m_pData;
        }

      private:
        // Info and value vectors share preset indices.
        struct PresetInfo {
            CryStringT<char> name;
            std::byte m_opaque0008[0x38];

          private:
            static void AssertLayout() {
                static_assert(offsetof(PresetInfo, name) == 0);
            }
        };
        static_assert(sizeof(PresetInfo) == 0x40);

        // Laid out by the constructor 0x180DA8758.
        void *m_vector0008[3];                  // 0x08, zeroed
        std::byte m_map0020[0x10];              // 0x20, an std::map: head node and size
        TimeOfDayClockStateLayout m_clockState; // 0x30
        void *m_pUpdateCallback;                // 0x80, optional callback consulted by Tick
        float m_realHour;                       // 0x88, the unfaked hour C_Calendar also publishes
        std::uint32_t m_padding008C;
        std::byte m_state0090[0x10];        // 0x90, zeroed
        std::byte *m_presetValuesBegin;     // 0xA0
        std::byte *m_presetValuesEnd;       // 0xA8
        std::byte *m_presetValuesCapacity;  // 0xB0
        PresetInfo *m_presetInfoBegin;      // 0xB8
        PresetInfo *m_presetInfoEnd;        // 0xC0
        PresetInfo *m_presetInfoCapacity;   // 0xC8
        std::uint32_t m_currentPresetIndex; // 0xD0, SetCurrentPreset
        std::uint32_t m_blendPresetIndex;   // 0xD4, SetPresetBlend, -1 when idle
        float m_blendFactor;                // 0xD8, SetPresetBlend
        std::uint32_t m_padding00DC;
        void *m_vector00E0[3];              // 0xE0, zeroed
        std::int32_t m_state00F8;           // 0xF8, -1 by default
        std::uint32_t m_state00FC;          // 0xFC

        static void AssertLayout() {
            static_assert(offsetof(CTimeOfDay, m_clockState) == 0x30, "CTimeOfDay::m_clockState offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_pUpdateCallback) == 0x80, "CTimeOfDay::m_pUpdateCallback offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_realHour) == 0x88, "CTimeOfDay::m_realHour offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_presetValuesBegin) == 0xA0, "CTimeOfDay::m_presetValuesBegin offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_presetInfoBegin) == 0xB8, "CTimeOfDay::m_presetInfoBegin offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_presetValuesEnd) == 0xA8);
            static_assert(offsetof(CTimeOfDay, m_presetValuesCapacity) == 0xB0);
            static_assert(offsetof(CTimeOfDay, m_presetInfoEnd) == 0xC0);
            static_assert(offsetof(CTimeOfDay, m_presetInfoCapacity) == 0xC8);
            static_assert(offsetof(CTimeOfDay, m_currentPresetIndex) == 0xD0, "CTimeOfDay::m_currentPresetIndex offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_blendPresetIndex) == 0xD4, "CTimeOfDay::m_blendPresetIndex offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_blendFactor) == 0xD8, "CTimeOfDay::m_blendFactor offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_map0020) == 0x20, "CTimeOfDay::m_map0020 offset mismatch");
            static_assert(offsetof(CTimeOfDay, m_state00F8) == 0xF8, "CTimeOfDay::m_state00F8 offset mismatch");
        }
    };

    static_assert(sizeof(CTimeOfDay) == 0x100, "CTimeOfDay size mismatch");
} // namespace SDK
