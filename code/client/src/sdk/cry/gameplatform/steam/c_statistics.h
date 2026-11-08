#pragma once

#include "../i_statistics.h"

#include <cstddef>
#include <cstdint>

namespace SDK::Cry::GamePlatform::Steam {
    /**
     * The Steam statistics service: the one object every achievement unlock
     * and progress update reaches, whichever path raised it.
     *
     * The game-side path goes through the rpgmodule achievement queue
     * (0x182D4F990), which C_UnlockAchievementEffect feeds from quest and RPG
     * rewards. The g_DebugUnlockAchievement and g_DebugProgressAchievement
     * console commands skip that queue and call this class directly.
     *
     * Layout from the constructor at 0x181DD1C30, allocated at 0xB8.
     */
    class CStatistics: public IStatistics {
      private:
        void *m_pService;            // 0x08 CService &
        void *m_pOwner;              // 0x10 the extra argument Warhorse's constructor takes
        std::byte m_callbacks[0x80]; // 0x18 four Steam CCallbacks: stats received, stored, unloaded, achievement stored
        std::uint32_t m_appId;       // 0x98
        bool m_bInitialized;         // 0x9C
        std::byte m_padding009D[0x3];
        std::byte m_achievements[0x18]; // 0xA0 std::vector<std::unique_ptr<IAchievement>>, allocated by the game

        static void AssertLayout() {
            static_assert(offsetof(CStatistics, m_appId) == 0x98, "CStatistics::m_appId offset mismatch");
            static_assert(offsetof(CStatistics, m_bInitialized) == 0x9C, "CStatistics::m_bInitialized offset mismatch");
            static_assert(offsetof(CStatistics, m_achievements) == 0xA0, "CStatistics::m_achievements offset mismatch");
        }
    };

    static_assert(sizeof(CStatistics) == 0xB8, "CStatistics size mismatch");
} // namespace SDK::Cry::GamePlatform::Steam
