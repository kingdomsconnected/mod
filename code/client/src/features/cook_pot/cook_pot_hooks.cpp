#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "cook_pot_service.h"

#include "core/application.h"
#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/game/c_script_bind_game.h"

#include <stdexcept>

namespace KCDC::Features::CookPot {
    namespace {
        using SetPrefabPhaseById_t                          = int(__fastcall *)(SDK::wh::game::CScriptBindGame *, SDK::IFunctionHandler *, SDK::ScriptHandle, const char *, int);
        SetPrefabPhaseById_t g_setPrefabPhaseByIdOriginal = nullptr;

        /**
         * `Game.SetPrefabPhaseById`: the only way so_fireplace.xml changes a
         * cauldron's look, called after every state the kettle brain writes,
         * and how it lights or puts out the fire and its steam. A catalogued
         * cauldron takes the server's phase instead, and its brain is put back
         * to the server's edibility on the way; a fire the server set keeps
         * the server's phase.
         */
        int __fastcall SetPrefabPhaseById_Hook(SDK::wh::game::CScriptBindGame *self, SDK::IFunctionHandler *handler, SDK::ScriptHandle entity, const char *category, int phase) {
            if (KCDC::Core::gApplication != nullptr) {
                phase = CookPotService::Get().OverridePhase(static_cast<SDK::EntityId>(entity), category, phase);
            }
            return g_setPrefabPhaseByIdOriginal(self, handler, entity, category, phase);
        }
    } // namespace
} // namespace KCDC::Features::CookPot

static InitFunction init(
    []() {
        // Without it local AI refills and empties pots on screen against the
        // server's stock, so fail loudly.
        if (MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CScriptBindGame__SetPrefabPhaseById), reinterpret_cast<LPVOID>(&KCDC::Features::CookPot::SetPrefabPhaseById_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::CookPot::g_setPrefabPhaseByIdOriginal)) != MH_OK) {
            throw std::runtime_error("Could not install the cook pot phase hook");
        }
    },
    "CookPot");
