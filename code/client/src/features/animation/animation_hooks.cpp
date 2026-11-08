#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "features/animation/animation_service.h"

#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/patterns.h"

#include <stdexcept>

namespace KCDC::Features::Animation {
    namespace {
        using Flush_t         = void(__fastcall *)(SDK::IActionController *);
        Flush_t s_flushNative = nullptr;

        // A flush drops every queued action with its status set to none and
        // no virtual call -- the one way an action leaves a controller that
        // its own slots never hear about. Everything installed has ended
        // through Exit by the time the native returns.
        void __fastcall FlushController(SDK::IActionController *controller) {
            s_flushNative(controller);
            AnimationService::Get().OnControllerFlushed(controller);
        }
    } // namespace
} // namespace KCDC::Features::Animation

static InitFunction init(
    []() {
        if (MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CActionController__Flush), reinterpret_cast<LPVOID>(&KCDC::Features::Animation::FlushController), reinterpret_cast<LPVOID *>(&KCDC::Features::Animation::s_flushNative)) != MH_OK) {
            throw std::runtime_error("Could not install the animation flush hook");
        }
    },
    "Animation");
