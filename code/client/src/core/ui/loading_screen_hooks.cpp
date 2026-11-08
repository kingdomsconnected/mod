#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/states/join_deadlines.h"
#include "sdk/patterns.h"
#include "sdk/wh/c_readiness_observer.h"
#include "sdk/wh/s_game_session_latch.h"

namespace KCDC::Core {
    namespace {
        // How long the screen after a sleep may wait for the world (seconds).
        constexpr float kSkipTimeReadinessSeconds = 2.0F;

        using ReadinessObserverStartFn                                = void(__fastcall *)(SDK::wh::C_ReadinessObserver *, std::uint32_t, void *);
        using ReadinessObserverStartExFn                              = void(__fastcall *)(SDK::wh::C_ReadinessObserver *, std::uint32_t, float, std::int32_t, void *);
        using RaiseLevelGameplayStartFn                               = void(__fastcall *)();
        using GameSessionLatchFn                                      = void(__fastcall *)(SDK::wh::S_GameSessionLatch *);
        ReadinessObserverStartFn g_readinessObserverStartOriginal     = nullptr;
        ReadinessObserverStartExFn g_readinessObserverStartExOriginal = nullptr;
        RaiseLevelGameplayStartFn g_raiseLevelGameplayStartOriginal   = nullptr;
        GameSessionLatchFn g_gameSessionStartOriginal                 = nullptr;
        GameSessionLatchFn g_gameSessionEndOriginal                   = nullptr;

        /** The game's session started (module message 52). Main thread; only latches. */
        void __fastcall GameSessionStart_Hook(SDK::wh::S_GameSessionLatch *latch) {
            g_gameSessionStartOriginal(latch);
            if (auto *app = gApplication.get()) {
                app->OnGameSessionLatch(true, latch);
            }
        }

        /** The game's session ended (module message 77). Main thread; only latches. */
        void __fastcall GameSessionEnd_Hook(SDK::wh::S_GameSessionLatch *latch) {
            g_gameSessionEndOriginal(latch);
            if (auto *app = gApplication.get()) {
                app->OnGameSessionLatch(false, latch);
            }
        }

        /**
         * An observer begins waiting on the readiness service.
         *
         * Every load completion waits here on `kLoadMask`: the save manager's
         * "AfterGameLoad" (which queues event 21 and takes the loading screen
         * down), `C_Game::OnLevelSwitchLoaded` (which ends the switch with
         * 4114 and so gameplay start) and `C_NewGameHelper`. The first one
         * after the session level is requested is therefore the edge "the
         * level has loaded and the game now waits to be ready" -- which is
         * exactly what the session's readiness task holds.
         *
         * Main thread, from C_Game's update; only latches. `callback` is a
         * std::function the callee takes by value, so it is passed on as is.
         */
        void __fastcall ReadinessObserverStart_Hook(SDK::wh::C_ReadinessObserver *observer, std::uint32_t mask, void *callback) {
            g_readinessObserverStartOriginal(observer, mask, callback);
            if (auto *app = gApplication.get(); app && mask == SDK::wh::C_ReadinessObserver::kLoadMask) {
                app->OnLoadReadinessAwaited();
            }
        }

        /**
         * Every observer's timeout passes through here (Start reads the
         * service's 50 s and forwards it; RequestSaveGame picks its own) and
         * is fixed as an absolute deadline before AddObserver. The load
         * completion the session's hold parks is started without one, so a
         * join whose download is still moving is never shown a world it has
         * not finished joining; the join's own deadlines refuse a stuck one.
         *
         * Main thread, from C_Game's update; `callback` is a std::function
         * the callee takes by value, so it is passed on as is.
         */
        void __fastcall ReadinessObserverStartEx_Hook(SDK::wh::C_ReadinessObserver *observer, std::uint32_t mask, float timeout, std::int32_t delayFrames, void *callback) {
            if (auto *app = gApplication.get()) {
                timeout = KCDC::Core::States::LoadReadinessTimeout(mask, SDK::wh::C_ReadinessObserver::kLoadMask, app->GetSessionLoadContext().active && app->IsSessionHeld(), timeout);
            }
            // After a sleep the skip screen waits on the world being ready
            // again. Some of its tasks never come ready in a session -- the
            // logs show the full 50 s every time, the player held behind the
            // screen -- so it is given a short wait instead, which a single
            // player game passes within it.
            if (mask == SDK::wh::C_ReadinessObserver::kSkipTimeMask && (timeout < 0.0F || timeout > kSkipTimeReadinessSeconds)) {
                timeout = kSkipTimeReadinessSeconds;
            }
            g_readinessObserverStartExOriginal(observer, mask, timeout, delayFrames, callback);
        }

        /**
         * The game's one producer of ESYSTEM_EVENT_LEVEL_GAMEPLAY_START, after
         * its listeners have run. It is queued behind the readiness wait, so
         * during a join it arrives once the session's task has released it.
         * Called from C_Game's deferred-call drain, under the engine lock, so
         * this only latches.
         */
        void __fastcall RaiseLevelGameplayStart_Hook() {
            g_raiseLevelGameplayStartOriginal();
            if (auto *app = gApplication.get()) {
                app->OnLevelGameplayStarted();
            }
        }
    } // namespace
} // namespace KCDC::Core

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ReadinessObserver__Start), reinterpret_cast<LPVOID>(&KCDC::Core::ReadinessObserverStart_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_readinessObserverStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ReadinessObserver__StartEx), reinterpret_cast<LPVOID>(&KCDC::Core::ReadinessObserverStartEx_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_readinessObserverStartExOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.GameSessionLatch__Start), reinterpret_cast<LPVOID>(&KCDC::Core::GameSessionStart_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_gameSessionStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.GameSessionLatch__End), reinterpret_cast<LPVOID>(&KCDC::Core::GameSessionEnd_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_gameSessionEndOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.guimodule__RaiseLevelGameplayStart), reinterpret_cast<LPVOID>(&KCDC::Core::RaiseLevelGameplayStart_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_raiseLevelGameplayStartOriginal));
    },
    "LoadingScreen");
