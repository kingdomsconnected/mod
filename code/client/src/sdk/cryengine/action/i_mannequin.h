#pragma once

#include "i_animation_database_manager.h"

namespace SDK {
    /**
     * `IGameFramework::GetMannequinInterface`, implemented by `CMannequinInterface` (vtable
     * 0x1840573A8; only the slots below are used, verified there).
     */
    class IMannequin {
      public:
        virtual ~IMannequin()  = default; // 0
        virtual void UnloadAll() = 0;     // 1
        virtual void ReloadAll() = 0;     // 2

        /** Slot 3: the manager the interface holds at +0x08 (a folded one-line getter, 0x180947210). */
        virtual IAnimationDatabaseManager &GetAnimationDatabaseManager() = 0; // 3
    };
} // namespace SDK
