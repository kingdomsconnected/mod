#pragma once

#include "../../../patterns.h"
#include "../../databasemodule/c_object_database_base.h"

#include <cstdint>

namespace SDK::wh::xgenaimodule::scheduler {
    /**
     * The AI scheduler's database (`C_SchedulerDatabase`, vtable 0x1840082E0),
     * a static the module's dynamic initializer builds (0x180E6EE38). Its
     * OnLoaded (0x1805C9B30) resolves the smart hubs against a list its first
     * load consumes, so a second load dereferences a hub it no longer finds:
     * it is never read again.
     */
    class C_SchedulerDatabase final: public databasemodule::C_ObjectDatabaseBase {
      public:
        static bool IsInstance(const databasemodule::I_ObjectDatabase *database) {
            return *reinterpret_cast<const std::uint64_t *>(database) == gPatterns.C_SchedulerDatabase__VTable;
        }
    };
} // namespace SDK::wh::xgenaimodule::scheduler
