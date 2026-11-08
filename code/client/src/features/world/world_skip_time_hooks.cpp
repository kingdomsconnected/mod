#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"
#include "sdk/wh/playermodule/c_skip_time.h"

#include "core/application.h"

namespace KCDC::Features::World {
    namespace {
        using BeginSkipTime_t                   = bool(__fastcall *)(SDK::wh::playermodule::C_SkipTime *, SDK::wh::playermodule::E_SkipTimeMode, float, float, const void *, int);
        BeginSkipTime_t g_beginSkipTimeOriginal = nullptr;

        /**
         * Suppresses the wait dial -- the clock the game puts up on T.
         *
         * The dial is the `open_skiptime` action C_UISkiptime registers, and
         * its handler does exactly one thing: call BeginSkipTime with
         * SDK::wh::playermodule::E_SkipTimeMode::Wait. No other caller passes that mode, so refusing
         * it here is the whole feature and leaves sleeping in a bed and timed
         * activities, which come in as Sleep and Activity, untouched.
         *
         * False is what the engine answers whenever CanSkipTime refuses, so
         * every caller already handles it -- and refusing at the entry is that
         * same state exactly: this happens before the active flag and the mode
         * are stored and before any action map is pushed, so nothing is left
         * half-opened and the action handler ignores the result anyway.
         */
        bool __fastcall BeginSkipTime_Hook(SDK::wh::playermodule::C_SkipTime *skipTime, SDK::wh::playermodule::E_SkipTimeMode mode, float from, float to, const void *parameters, int extra) {
            if (mode == SDK::wh::playermodule::E_SkipTimeMode::Wait) {
                return false;
            }
            // A knockout's skip runs by speeding the world clock up, which a
            // session that owns the calendar refuses (world_hooks.cpp): nothing
            // was skipped, and its screen held input and menus for the whole
            // knockout. One player cannot skip the clock everyone shares, so it
            // is refused as the wait dial is; the knockout itself plays on, and
            // how long it lasts is the server's to say.
            if (mode == SDK::wh::playermodule::E_SkipTimeMode::Unconscious) {
                return false;
            }

            // Sleep runs the local clock for its while (WorldService::IsSleepRunningClock).
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetWorldService().NoteSkipTime(skipTime);
            }
            return g_beginSkipTimeOriginal(skipTime, mode, from, to, parameters, extra);
        }
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SkipTime__BeginSkipTime), reinterpret_cast<LPVOID>(&KCDC::Features::World::BeginSkipTime_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_beginSkipTimeOriginal));
    },
    "SkipTime");
