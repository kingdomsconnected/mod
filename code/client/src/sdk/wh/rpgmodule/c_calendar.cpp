#include "../c_wh_global_environment.h"
#include "c_calendar.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cmath>

namespace SDK::wh::rpgmodule {
    C_Calendar *C_Calendar::GetInstance() {
        const auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();

        return environment != nullptr ? environment->GetCalendar() : nullptr;
    }

    void C_Calendar::SetWorldTimeMilliseconds(std::int64_t milliseconds) {
        hook::this_call<void>(SDK::gPatterns.C_Calendar__SetWorldTime, this, milliseconds);
    }

    void C_Calendar::AdoptWorldTimeMilliseconds(std::int64_t milliseconds) {
        if (milliseconds >= m_worldTimeMilliseconds) {
            SetWorldTimeMilliseconds(milliseconds);

            return;
        }

        const auto previousDay = GetWorldDay();

        // Seed an equal value so the native setter accepts the earlier epoch.
        m_worldTimeMilliseconds = milliseconds;
        SetWorldTimeMilliseconds(milliseconds);

        // Restore the day-change notification after the absolute-time signal.
        if (previousDay != GetWorldDay()) {
            hook::this_call<void>(SDK::gPatterns.C_Signal180875DF8__Broadcast, &m_dayChangedListeners);
        }
    }

    std::int64_t C_Calendar::SetHourOfDay(float hour) {
        const float wrapped = std::fmod(std::max(hour, 0.0F), 24.0F);
        std::int64_t target = GetWorldDay() * kMillisecondsPerDay + static_cast<std::int64_t>(wrapped * static_cast<float>(kMillisecondsPerHour));

        if (target < m_worldTimeMilliseconds) {
            target += kMillisecondsPerDay;
        }

        SetWorldTimeMilliseconds(target);

        return target;
    }

    void C_Calendar::SetWorldTimeRatio(float ratio) {
        hook::this_call<void>(SDK::gPatterns.C_Calendar__SetWorldTimeRatio, this, ratio);
    }

    void C_Calendar::PublishTimeOfDay(bool force) {
        hook::this_call<void>(SDK::gPatterns.C_Calendar__PublishTimeOfDay, this, force);
    }

    void C_Calendar::SetFakeTimeOfDay(float hour) {
        m_fakeTimeOfDayHour = std::fmod(std::max(hour, 0.0F), 24.0F);
        m_fakeTimeOfDay     = 1;
        PublishTimeOfDay(true);
    }

    void C_Calendar::ClearFakeTimeOfDay() {
        m_fakeTimeOfDay = 0;
        PublishTimeOfDay(true);
    }
} // namespace SDK::wh::rpgmodule
