#pragma once

#include "c_combat_actor_updated_object.h"
#include "i_combat_environment_manager.h"

#include <cstddef>

namespace SDK::wh::combatmodule {
    /** Update-aware manager for combat environment and obstacle state. */
    class C_CombatEnvironmentManager
        : public C_CombatActorUpdatedObject,
          public I_CombatEnvironmentManager {
      public:
        ~C_CombatEnvironmentManager() override = default;

      private:
        // Constructor 0x18091784C builds both sub-objects in place.
        std::byte m_spatialTest[0x30];  // 0x20; wh::combatmodule::C_CombatSpatialTest (ctor sub_180917D04)
        std::byte m_obstacleTest[0x50]; // 0x50; wh::combatmodule::C_CombatObstacleTest (ctor sub_180917B50)

        static void AssertLayout() {
            static_assert(offsetof(C_CombatEnvironmentManager, m_spatialTest) == 0x20, "C_CombatEnvironmentManager::m_spatialTest offset mismatch");
            static_assert(offsetof(C_CombatEnvironmentManager, m_obstacleTest) == 0x50, "C_CombatEnvironmentManager::m_obstacleTest offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatEnvironmentManager) == 0xA0,
                  "C_CombatEnvironmentManager size mismatch");
} // namespace SDK::wh::combatmodule
