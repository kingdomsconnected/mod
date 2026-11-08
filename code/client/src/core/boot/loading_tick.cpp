#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/boot/boot_state.h"
#include "core/boot/init_patches.h"

#include "sdk/patterns.h"

namespace KCDC::Core::Boot {
    namespace {
        using CSystemSynchronousLoadingTickFn = bool(__fastcall *)(void *);
        CSystemSynchronousLoadingTickFn g_loadingTickOriginal = nullptr;

        // The engine services its own loading work on a 20ms throttle. The
        // peer only has to beat the challenge stopwatch, so it is pumped a good
        // deal less often than that.
        constexpr std::uint64_t kPumpIntervalMs = 100;

        /**
         * The engine's loading-time service pump, and the only thing that runs
         * on the main thread while a level load holds it.
         *
         * A load takes that thread for fifteen seconds and more, and
         * CCryAction::PostUpdate -- which drives the whole mod, the peer
         * included -- does not run once in all that time. The peer surviving
         * the load is not the problem: MafiaNet keeps sending and answering on
         * its own thread throughout, and the server's build reply lands within
         * a hundred milliseconds. What starves is the reading of it. The build
         * challenge is a wall-clock stopwatch popped from our own pump, so it
         * expires against a reply already sitting in the queue, and the server
         * is then blamed for a version mismatch it never reported.
         *
         * The engine reaches here from the XML, CGF, texture, material and
         * 3D-engine loading paths, which is what makes it cover a whole load
         * rather than a single phase of one.
         */
        bool __fastcall CSystem_SynchronousLoadingTick_Hook(void *system) {
            const bool keepLoading = g_loadingTickOriginal(system);

            // Before the application guard: the reel plays during the engine's
            // own boot, long before there is an application to speak of.
            KillIntroMovie();

            // The engine enters this from its streaming threads as well as
            // from the one it ticks the game on, and the peer is not a thing
            // two threads may be inside at once: `RakPeer::Receive` drives
            // `RakVoice::Update`, whose Opus encoder is being fed frames by the
            // game thread at the same moment. That combination aborts the
            // process inside the SILK resampler on a torn frame.
            //
            // Nothing is lost by refusing the others. This pump exists because
            // a load takes the game thread for fifteen seconds and more, and
            // the engine's loading ticks run on that same thread while it does
            // -- so the case it was written for still reaches it. A load that
            // runs on a streaming thread instead leaves the game thread free,
            // and the peer is being pumped from the ordinary frame anyway.
            if (KCDC::Core::Boot::gBootState.gameThreadId.load(std::memory_order_relaxed) != GetCurrentThreadId()) {
                return keepLoading;
            }

            // Nor from inside the application's own tick. A body the spawner
            // builds there loads its models, and the engine ticks its loading
            // pump for those too; pumped then, the peer deletes the very replica
            // being spawned, whose callback then runs on freed memory. The tick
            // is the frame that pumps the peer anyway, and a level load starts
            // in the native frame, outside it.
            if (KCDC::Core::Boot::gBootState.inApplicationTick) {
                return keepLoading;
            }

            const std::uint64_t now = GetTickCount64();
            if (now - KCDC::Core::Boot::gBootState.lastPumpMs < kPumpIntervalMs) {
                return keepLoading;
            }
            KCDC::Core::Boot::gBootState.lastPumpMs = now;

            // Only the peer, never a whole client tick: this is entered from
            // inside the engine's loading operations, and the rest of a tick
            // has no business touching a half-built world. The spawn barrier is
            // what holds replicas back until the level is up, so nothing that
            // arrives here can start syncing early.
            auto *app = gApplication.get();
            if (app == nullptr || !app->IsInitialized()) {
                return keepLoading;
            }
            if (auto *engine = app->GetNetworkingEngine()) {
                if (auto *client = engine->GetNetworkClient()) {
                    client->Update();
                }
            }
            return keepLoading;
        }
    } // namespace
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CSystem__SynchronousLoadingTick), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CSystem_SynchronousLoadingTick_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_loadingTickOriginal));
    },
    "LoadingTick");
