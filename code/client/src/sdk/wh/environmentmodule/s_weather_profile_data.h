#pragma once

#include "../../types.h"
#include "../playermodule/s_tutorial.h"

#include <cstdint>
#include <vector>

namespace SDK::wh::framework {
    /** Opaque scalar storage used by reflected time-of-day properties. */
    struct S_TimeOfDay {
        std::uint64_t m_value;
    };

    static_assert(sizeof(S_TimeOfDay) == 0x8,
                  "S_TimeOfDay size mismatch");
} // namespace SDK::wh::framework

namespace SDK::wh::environmentmodule {
    /**
     * Reflected weather-profile record consumed by C_WeatherProfileDatabase.
     *
     * The RTTR registration at 0x180E37CE0 proves every property offset and
     * the TypeID registration at 0x181965450 proves the complete 0x48 extent.
     *
     * Only Name is a string: the copy constructor at 0x180E3A410 refcounts
     * 0x00 alone and copies 0x08 through 0x28 as plain qwords, and RTTR
     * registers one CryStringT<char> wrapper against three S_Condition ones.
     */
    struct S_WeatherProfileData {
        struct S_Condition {
            float m_min; // 0x00, RTTR: Min
            float m_max; // 0x04, RTTR: Max
        };

        struct S_TimeOfDayInterval {
            framework::S_TimeOfDay m_from; // 0x00, RTTR: From
            framework::S_TimeOfDay m_to;   // 0x08, RTTR: To
        };

        CryStringT<char> m_name;                         // 0x00, RTTR: Name
        framework::S_TimeSpan m_delayMin;                // 0x08, RTTR: DelayMin
        framework::S_TimeSpan m_delayMax;                // 0x10, RTTR: DelayMax
        S_Condition m_temperature;                       // 0x18, RTTR: Temperature
        S_Condition m_rainIntensity;                     // 0x20, RTTR: RainIntensity
        S_Condition m_windVelocity;                      // 0x28, RTTR: WindVelocity
        // 0x30, RTTR: TimeOfDayIntervals
        std::vector<S_TimeOfDayInterval> m_timeOfDayIntervals;
    };

    static_assert(sizeof(S_WeatherProfileData::S_Condition) == 0x8,
                  "S_WeatherProfileData::S_Condition size mismatch");
    static_assert(sizeof(S_WeatherProfileData::S_TimeOfDayInterval) == 0x10,
                  "S_WeatherProfileData::S_TimeOfDayInterval size mismatch");
    static_assert(sizeof(S_WeatherProfileData) == 0x48,
                  "S_WeatherProfileData size mismatch");
} // namespace SDK::wh::environmentmodule
