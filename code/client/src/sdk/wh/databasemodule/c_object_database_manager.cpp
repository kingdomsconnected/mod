#include "c_object_database_manager.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::databasemodule {
    C_ObjectDatabaseManager *C_ObjectDatabaseManager::GetInstance() {
        return hook::call<C_ObjectDatabaseManager *>(gPatterns.C_ObjectDatabaseManager__Instance);
    }

    bool C_ObjectDatabaseManager::ReloadDatabases(const ObjectDatabaseList &databases, bool reload, bool force) {
        return hook::this_call<bool>(gPatterns.C_ObjectDatabaseManager__ReloadDatabases, this, &databases, reload, force);
    }
} // namespace SDK::wh::databasemodule
