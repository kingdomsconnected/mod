#pragma once

#include "../environmentmodule/s_weather_profile_data.h"
#include "c_object_database_base.h"

#include <cstddef>
#include <vector>

namespace SDK::wh::databasemodule {
    /**
     * Typed Warhorse object-database layer.
     *
     * RTTI preserves this template between C_ObjectTreeDatabase and
     * C_ObjectDatabaseBase. The weather-profile specialization adds no
     * virtual slots and extends the base layout from 0x30 to 0x48.
     */
    template <typename TObject,
              template <typename, typename> class TContainer>
    class C_ObjectDatabase : public C_ObjectDatabaseBase {
      public:
        ~C_ObjectDatabase() override = default;

        const TObject *ObjectsBegin() const { return m_objectsBegin; }
        const TObject *ObjectsEnd() const { return m_objectsEnd; }

        /** Records the database currently holds; zero before it is loaded. */
        std::size_t ObjectCount() const {
            if (m_objectsBegin == nullptr || m_objectsEnd <= m_objectsBegin) {
                return 0;
            }
            return static_cast<std::size_t>(m_objectsEnd - m_objectsBegin);
        }

      private:
        // The std::vector<TObject> this layer adds. The weather-profile
        // constructor clears exactly +0x30, +0x38 and +0x40, and its loader
        // walks begin..end with a sizeof(TObject) stride.
        TObject *m_objectsBegin;    // 0x30
        TObject *m_objectsEnd;      // 0x38
        TObject *m_objectsCapacity; // 0x40
    };

    using C_WeatherProfileObjectDatabase =
        C_ObjectDatabase<environmentmodule::S_WeatherProfileData,
                         std::vector>;

    static_assert(sizeof(C_WeatherProfileObjectDatabase) == 0x48,
                  "weather object-database size mismatch");
} // namespace SDK::wh::databasemodule
