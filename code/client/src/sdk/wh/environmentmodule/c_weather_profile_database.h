#pragma once

#include "../databasemodule/c_object_tree_database.h"

#include <vector>

namespace SDK::wh::environmentmodule {
    /**
     * Concrete database of KCD2 weather profiles.
     *
     * RTTI proves the complete hierarchy through the Warhorse typed/tree
     * database templates. Its table at 0x183AB87A0 has exactly the 22
     * inherited entries, with no concrete additions or additional state.
     */
    class C_WeatherProfileDatabase
        : public databasemodule::C_ObjectTreeDatabase<S_WeatherProfileData,
                                                       std::vector> {
      public:
        ~C_WeatherProfileDatabase() override = default;

        static C_WeatherProfileDatabase *GetInstance();
    };

    static_assert(sizeof(C_WeatherProfileDatabase) == 0x78,
                  "C_WeatherProfileDatabase size mismatch");
} // namespace SDK::wh::environmentmodule
