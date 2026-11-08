#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/obfuscation.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_butchering.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <stdexcept>

namespace KCDC::Features::Npc {
    namespace {
        using Exchange_t                 = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, SDK::EntityId);
        using ButcherEnter_t             = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionButchering *, void *);
        using ButcherEvent_t             = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionButchering *);
        using CanButcher_t               = bool(__fastcall *)(const SDK::wh::rpgmodule::I_Soul *);
        Exchange_t g_actorExchange       = nullptr;
        Exchange_t g_humanExchange       = nullptr;
        ButcherEnter_t g_butcherEnter    = nullptr;
        ButcherEvent_t g_butcherComplete = nullptr;
        ButcherEvent_t g_butcherExit     = nullptr;
        CanButcher_t g_canButcher        = nullptr;

        void __fastcall ActorExchange(SDK::wh::entitymodule::C_Actor *actor, SDK::EntityId user) {
            if (KCDC::Core::gApplication != nullptr && actor != nullptr && KCDC::Core::gApplication->GetLocalEvents().NpcItemExchange(*actor, user)) {
                return;
            }
            g_actorExchange(actor, user);
        }
        void __fastcall HumanExchange(SDK::wh::entitymodule::C_Actor *actor, SDK::EntityId user) {
            if (KCDC::Core::gApplication != nullptr && actor != nullptr && KCDC::Core::gApplication->GetLocalEvents().NpcItemExchange(*actor, user)) {
                return;
            }
            g_humanExchange(actor, user);
        }
        bool __fastcall ButcherEnter(SDK::wh::entitymodule::C_ActorActionButchering *action, void *context) {
            if (KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetLocalEvents().NpcButcheringStarting(*action)) {
                return false;
            }
            const bool accepted = g_butcherEnter(action, context);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NpcButcheringEntered(*action, accepted);
            }
            return accepted;
        }
        void __fastcall ButcherComplete(SDK::wh::entitymodule::C_ActorActionButchering *action) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().NpcButcheringCompleted(*action)) {
                return;
            }
            g_butcherComplete(action);
        }
        void __fastcall ButcherExit(SDK::wh::entitymodule::C_ActorActionButchering *action) {
            const bool cancelled = action->WasCancelled();
            g_butcherExit(action);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NpcButcheringExited(*action, cancelled);
            }
        }
        bool __fastcall CanButcher(const SDK::wh::rpgmodule::I_Soul *soul) {
            bool answer = false;
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().NpcCanBeButchered(soul, answer)) {
                return answer;
            }
            return g_canButcher(soul);
        }
    } // namespace
} // namespace KCDC::Features::Npc

static InitFunction init(
    []() {
        if (MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__RequestItemExchange), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::ActorExchange), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_actorExchange)) != MH_OK
            || MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Human__RequestItemExchange), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::HumanExchange), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_humanExchange)) != MH_OK
            || MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionButchering__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::ButcherEnter), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_butcherEnter)) != MH_OK
            || MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionButchering__Complete), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::ButcherComplete), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_butcherComplete)) != MH_OK
            || MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionButchering__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::ButcherExit), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_butcherExit)) != MH_OK
            || MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__CanBeButchered), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::CanButcher), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_canButcher)) != MH_OK) {
            throw std::runtime_error(std::string(std::string_view(KCDC_HIDE("Could not install corpse inventory hooks"))));
        }
    },
    "Interaction");
