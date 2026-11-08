#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/ui/ui_hud_screen_effects.h"

#include "sdk/patterns.h"
#include "sdk/wh/guimodule/c_ui_overlay.h"

namespace KCDC::Features::UI {
    namespace {
        using ApplyUserColor_t = void(__fastcall *)(SDK::wh::guimodule::C_UIOverlay *, bool);

        ApplyUserColor_t g_applyUserColorOriginal = nullptr;

        /**
         * The game publishing its own screen tint.
         *
         * `C_UIOverlay::ApplyUserColor` is the one place the game writes the
         * eight `Global_User_*` parameters in normal play, each as a forced
         * write, and only when the player's condition or the overlays'
         * visibility changes. Letting it run and then putting the composition
         * back on top is what keeps a script's grade on screen without
         * re-asserting it every frame -- and without ever hiding the game's
         * own tint, which the composition starts from.
         */
        void __fastcall ApplyUserColor_Hook(SDK::wh::guimodule::C_UIOverlay *overlay, bool enabled) {
            g_applyUserColorOriginal(overlay, enabled);
            HudScreenEffects::Get().OnUserColorApplied();
        }
    } // namespace

    void HudScreenEffects::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIOverlay__ApplyUserColor), reinterpret_cast<LPVOID>(&ApplyUserColor_Hook), reinterpret_cast<LPVOID *>(&g_applyUserColorOriginal));
    }
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        KCDC::Features::UI::HudScreenEffects::Install();
    },
    "ScreenEffects");
