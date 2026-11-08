#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>

#include <utils/hooking/hook_function.h>

#include "sdk/cry/gameplatform/i_statistics.h"
#include "sdk/patterns.h"

namespace KCDC::Features::World {
    namespace {
        /**
         * Keeps a multiplayer session off the player's Steam or Xbox achievements.
         *
         * rpgmodule reaches the platform's statistics service only through this
         * getter: the achievement queue's unlock and the achievement reset both
         * ask it, and both skip when it answers null -- the answer the game
         * already gives when no platform service is running, logging that the
         * achievement could not be unlocked. Neither Steam nor Xbox is told.
         */
        SDK::Cry::GamePlatform::IStatistics *__fastcall GetPlatformStatistics_Hook() {
            return nullptr;
        }

        /**
         * The g_DebugUnlockAchievement and g_DebugProgressAchievement console
         * commands fetch the service themselves, so they are refused as well.
         */
        void __fastcall DebugAchievementCommand_Hook(void *) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Achievement console command refused");
        }
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.rpgmodule__GetPlatformStatistics), reinterpret_cast<LPVOID>(&KCDC::Features::World::GetPlatformStatistics_Hook), nullptr);
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ConsoleCommand__DebugUnlockAchievement), reinterpret_cast<LPVOID>(&KCDC::Features::World::DebugAchievementCommand_Hook), nullptr);
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ConsoleCommand__DebugProgressAchievement), reinterpret_cast<LPVOID>(&KCDC::Features::World::DebugAchievementCommand_Hook), nullptr);
    },
    "Achievements");
