#include "c_weather_profile_database.h"

#include "../../patterns.h"

namespace SDK::wh::environmentmodule {
    C_WeatherProfileDatabase *C_WeatherProfileDatabase::GetInstance() {
        return reinterpret_cast<C_WeatherProfileDatabase *>(
            gPatterns.C_WeatherProfileDatabase__Instance);
    }
} // namespace SDK::wh::environmentmodule
