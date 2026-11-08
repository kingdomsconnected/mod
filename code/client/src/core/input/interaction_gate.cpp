#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include "features/cook_pot/cook_pot_service.h"
#include "features/door/door_service.h"
#include "features/stash/stash_service.h"

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"

#include <cstdint>
#include <stdexcept>

// The native interaction gate: every prompt an entity's Lua GetActions
// returns, and every press of one, passes these two entries. Each feature that
// holds a press for the server answers here in turn; a press none claims runs
// as the game would have run it.
namespace KCDC::Core::Input {
    namespace {
        using AddLuaAction_t        = void(__fastcall *)(SDK::wh::entitymodule::C_PlayerInteractor *, SDK::wh::entitymodule::S_InteractorActionRecord *, SDK::wh::entitymodule::E_InteractionType, std::uint64_t);
        using InvokeLuaActionFunc_t = void(__fastcall *)(void *, SDK::wh::entitymodule::S_LuaActionInvocation *);

        AddLuaAction_t g_addLuaActionOriginal               = nullptr;
        InvokeLuaActionFunc_t g_invokeLuaActionFuncOriginal = nullptr;

        /**
         * Every prompt is registered here, and this is the only place its
         * interaction type, its handler and its target are in hand together.
         * The record is the caller's, read by the original before it keeps a
         * copy, so a prompt disabled here is registered disabled. The handler
         * is a pointer-sized registry index, so the whole register is
         * forwarded untouched.
         */
        void __fastcall AddLuaAction_Hook(SDK::wh::entitymodule::C_PlayerInteractor *interactor, SDK::wh::entitymodule::S_InteractorActionRecord *record, SDK::wh::entitymodule::E_InteractionType interactionType, std::uint64_t function) {
            if (KCDC::Core::gApplication != nullptr && interactor != nullptr && record != nullptr) {
                const auto handler = static_cast<SDK::HScriptFunction>(function);
                KCDC::Core::gApplication->GetLocalEvents().NpcActionRegistering(*interactor, *record, handler);
                KCDC::Core::gApplication->GetStashService().OnActionRegistering(*interactor, *record, interactionType, handler);
                KCDC::Features::CookPot::CookPotService::Get().OnActionRegistering(*interactor, *record, handler);
                KCDC::Core::gApplication->GetDoorService().OnActionRegistering(*interactor, interactionType, handler);
            }
            g_addLuaActionOriginal(interactor, record, interactionType, function);
        }

        /**
         * A pressed prompt, about to run its Lua handler. A feature that holds
         * it for the server returns true and nothing of it runs; that feature
         * replays it through this same entry once the server allows it. A
         * door's press a client script refused returns true and is dropped.
         */
        void __fastcall InvokeLuaActionFunc_Hook(void *callback, SDK::wh::entitymodule::S_LuaActionInvocation *invocation) {
            if (KCDC::Core::gApplication != nullptr && invocation != nullptr) {
                if (KCDC::Core::gApplication->GetLocalEvents().NpcActionInvoked(*invocation) || KCDC::Core::gApplication->GetStashService().InterceptInteraction(*invocation) || KCDC::Features::CookPot::CookPotService::Get().InterceptInteraction(*invocation)
                    || KCDC::Core::gApplication->GetDoorService().InterceptInteraction(*invocation)) {
                    return;
                }
            }
            g_invokeLuaActionFuncOriginal(callback, invocation);
        }
    } // namespace
} // namespace KCDC::Core::Input

static InitFunction init(
    []() {
        // Without the gate every held interaction runs unasked and the server
        // refuses what follows, so fail loudly.
        const MH_STATUS addLuaActionStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInteractor__AddLuaAction), reinterpret_cast<LPVOID>(&KCDC::Core::Input::AddLuaAction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Input::g_addLuaActionOriginal));
        const MH_STATUS invokeStatus       = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.entitymodule__InvokeLuaActionFunc), reinterpret_cast<LPVOID>(&KCDC::Core::Input::InvokeLuaActionFunc_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Input::g_invokeLuaActionFuncOriginal));
        if (addLuaActionStatus != MH_OK || invokeStatus != MH_OK) {
            throw std::runtime_error("Could not install the interaction gate");
        }
    },
    "InteractionGate");
