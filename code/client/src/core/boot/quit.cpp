#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include <string_view>

#include "core/application.h"

#include "sdk/patterns.h"

namespace KCDC::Core::Boot {
    namespace {
        // Sixteen bytes, so the reason arrives by hidden pointer.
        using CSystemQuitFn = void(__fastcall *)(void *, const std::string_view *, unsigned int);
        CSystemQuitFn g_quitOriginal = nullptr;

        /**
         * Every way out of the game lands here, and the reason says which.
         *
         * Teardown runs ahead of the original because CSystem::Quit can end in
         * TerminateProcess: no DLL_PROCESS_DETACH, no atexit, no static
         * destruction. The native has no re-entry guard of its own, so
         * IsInitialized() is ours -- the framework lowers it in Shutdown().
         */
        void __fastcall CSystem_Quit_Hook(void *system, const std::string_view *reason, unsigned int exitCode) {
            if (auto *app = gApplication.get(); app && app->IsInitialized()) {
                // Last line the mod writes: Shutdown() takes the logging pool.
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Engine quit requested (exit code {}): {}", exitCode, *reason);
                app->Shutdown();
            }

            g_quitOriginal(system, reason, exitCode);
        }
    } // namespace
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CSystem__Quit), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CSystem_Quit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_quitOriginal));
    },
    "Quit");
