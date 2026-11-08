#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/boot/boot_state.h"

#include "sdk/patterns.h"

namespace KCDC::Core::Boot {
    namespace {
        using CCryActionPostUpdateFn = void(__fastcall *)(void *, bool, unsigned int);
        CCryActionPostUpdateFn g_postUpdateOriginal = nullptr;

        // Networking and remote placement run after the native frame.
        void __fastcall CCryAction_PostUpdate_Hook(void *cryAction, bool haveFocus, unsigned int updateFlags) {
            // Stated before the native frame rather than after it, because the
            // native frame is what can start a load, and the loading pump has
            // to already know which thread this one is.
            KCDC::Core::Boot::gBootState.gameThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);

            g_postUpdateOriginal(cryAction, haveFocus, updateFlags);

            if (auto *app = gApplication.get(); app && app->IsInitialized()) {
                KCDC::Core::Boot::gBootState.inApplicationTick = true;
                app->Update();
                KCDC::Core::Boot::gBootState.inApplicationTick = false;
            }
        }
    } // namespace
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CCryAction__PostUpdate), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CCryAction_PostUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_postUpdateOriginal));
    },
    "Engine");
