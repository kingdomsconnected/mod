#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/patterns.h"
#include "core/application.h"

#include <stdexcept>

namespace KCDC::Features::Social {
    namespace {
        using ActionEdge   = void(__fastcall *)(SDK::IAction *);
        ActionEdge s_enter = nullptr;
        ActionEdge s_exit  = nullptr;

        void __fastcall EmoteEnter(SDK::IAction *action) {
            s_enter(action);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetEmoteService().NotifyActionEntered(action);
            }
        }

        // Several IAction specializations fold their Fail and their ordinary
        // Exit into this one body.
        void __fastcall SharedActionFailOrExit(SDK::IAction *action) {
            s_exit(action);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetEmoteService().NotifyActionExited(action);
            }
        }
    } // namespace
} // namespace KCDC::Features::Social

static InitFunction init(
    []() {
        const auto enterStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.IAction__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Social::EmoteEnter), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::s_enter));
        const auto exitStatus  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.IAction__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Social::SharedActionFailOrExit), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::s_exit));
        if (enterStatus != MH_OK || exitStatus != MH_OK) {
            throw std::runtime_error("Could not install native emote lifecycle hooks");
        }
    },
    "Emotes");
