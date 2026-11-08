#pragma once

namespace SDK {
    /**
     * One loaded Mannequin animation database (`.adb`), implemented by `CAnimationDatabase`.
     *
     * Only ever handed back to the engine -- to `CAnimationDatabaseManager::ReloadDatabase` -- so
     * none of its interface is declared; its fragments are read through the action controller.
     */
    class IAnimationDatabase {
      public:
        IAnimationDatabase()                                      = delete;
        IAnimationDatabase(const IAnimationDatabase &)            = delete;
        IAnimationDatabase &operator=(const IAnimationDatabase &) = delete;
    };
} // namespace SDK
