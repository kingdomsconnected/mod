#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"
#include "sdk/wh/playermodule/c_tutorial_manager.h"

namespace KCDC::Features::UI {
    namespace {
        /**
         * Suppresses every popup and overlay tutorial.
         *
         * ShowTutorial is the only entry that puts a tutorial on screen, so
         * cutting it covers all of them. The game's own wh_ui_TutorialsEnabled
         * option does not: CanShowTutorial reads it as `IsSystemOverlay ||
         * AreTutorialsEnabled()`, and the hints that appear on menu screens
         * carry IsSystemOverlay.
         *
         * False is what the engine itself answers whenever the option is off
         * or the tutorial has been seen, so callers already handle it. Leaving
         * the original uncalled also keeps the manager's shown collection
         * untouched, so nothing is recorded and no save-state is carried.
         */
        bool __fastcall ShowTutorial_Hook(SDK::wh::playermodule::C_TutorialManager *, const void *) {
            return false;
        }
    } // namespace
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_TutorialManager__ShowTutorial), reinterpret_cast<LPVOID>(&KCDC::Features::UI::ShowTutorial_Hook), nullptr);
    },
    "Tutorial");
