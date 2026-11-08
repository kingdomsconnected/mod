#include "core/boot/init_patches.h"

#include <utils/hooking/hook_function.h>

#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>

#include "features/world/world_population.h"

#include "sdk/cryengine/renderer/i_renderer.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/patterns.h"

namespace KCDC::Core::Boot {
    namespace {
        using CD3D9Renderer_StartRenderIntroMovies_t = void(__fastcall *)(void *);
        CD3D9Renderer_StartRenderIntroMovies_t g_startRenderIntroMoviesOriginal = nullptr;

        using C_UIMenu_ShowFirstTimeOverlays_t = bool(__fastcall *)(void *);
        C_UIMenu_ShowFirstTimeOverlays_t g_showFirstTimeOverlaysOriginal = nullptr;

        using CGame_CompleteInit_t = char(__fastcall *)(void *);
        CGame_CompleteInit_t g_completeInitOriginal = nullptr;

        // CSystem::Init starts the boot logo reel here whenever
        // sys_intromoviesduringinit is set, then CCryAction::CompleteInit blocks
        // the main thread in StopRenderIntroMovies until the Bink stream drains.
        //
        // Swallowing the call leaves m_pIntroMovieRenderer null, which is
        // exactly the state the engine reaches with sys_intromoviesduringinit
        // 0, so the later stop short-circuits on its own null check instead of
        // spinning on Sleep(10).
        //
        // Alone among the switches here this one has to be in place ahead of
        // the engine: CSystem::Init reads the CVar exactly once, and nothing
        // later can put the reel back.
        void __fastcall CD3D9Renderer_StartRenderIntroMovies_Hook(void *) {
            // Whether the hook beat CSystem::Init is otherwise invisible: a
            // reel that started before it was armed looks exactly like one
            // that was never requested.
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Boot logo reel suppressed");
        }

        // The menu controller asks this before it opens the main menu, and
        // bails out when the answer is true so the queued EULA and
        // telemetry-consent pages get their turn first. Reporting that nothing
        // was queued lets the menu open on the first attempt.
        //
        // This is the same door the game's own wh_ui_showFirstTimeOverlays 0
        // opens; nothing is written back, so no consent is recorded on the
        // player's behalf.
        bool __fastcall C_UIMenu_ShowFirstTimeOverlays_Hook(void *) {
            return false;
        }

        // The console does not exist when the mod attaches, so these are driven
        // from the first point where CryEngine guarantees a live one. The game
        // sets them through this same path when launched with +map.
        char __fastcall CGame_CompleteInit_Hook(void *game) {
            // Backstop for a reel the loading pump never got a tick to end.
            KillIntroMovie();

            // Ahead of the original, because the original is what reads them.
            //
            // StopRenderIntroMovies runs inside CCryAction::CompleteInit and
            // reads wh_sys_SkipStartupVideoWhenPossible at that moment: set, it
            // calls CIntroMovieRenderer::Stop; clear, it holds the main thread
            // in a Sleep(10) loop until the Bink drains. Set afterwards this
            // was never a backstop at all -- the reel had already been watched
            // to the end by the time the write landed.
            SDK::IConsole::SetIntCVar("wh_sys_SkipStartupVideoWhenPossible", 1);

            // Steers C_UILoadingScreen::SetState's new_game branch away from
            // the intro movie and onto the plain loading screen. SetState
            // re-reads the CVar on every transition, so this is not a race.
            SDK::IConsole::SetIntCVar("wh_sys_HideIntroVideo", 1);

            const char result = g_completeInitOriginal(game);

#if defined(KCDC_AGENT_TESTING)
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing graphics: {}x{}, fullscreen={}, HDR={}, quality={}", SDK::IConsole::GetIntCVar("r_Width", -1), SDK::IConsole::GetIntCVar("r_Height", -1), SDK::IConsole::GetIntCVar("r_Fullscreen", -1), SDK::IConsole::GetIntCVar("r_HDROutput", -1), SDK::IConsole::GetIntCVar("sys_spec_Full", -1));
#endif

            // The entity system, so this one waits for the original.
            KCDC::Features::World::InstallPopulationSink();

            return result;
        }
    } // namespace

    void KillIntroMovie() {
        // Deterministic, not a race. The engine guards this on its own
        // m_pIntroMovieRenderer, so it is a no-op before the reel exists
        // and after it is gone, and `false` skips the drain the game does
        // for itself at CompleteInit. Whether the suppression hook was
        // armed in time stops mattering: if the reel is up, this ends it.
        if (auto *renderer = SDK::IRenderer::GetInstance()) {
            renderer->StopRenderIntroMovies(false);
        }
    }
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CD3D9Renderer__StartRenderIntroMovies), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CD3D9Renderer_StartRenderIntroMovies_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_startRenderIntroMoviesOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenu__ShowFirstTimeOverlays), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::C_UIMenu_ShowFirstTimeOverlays_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_showFirstTimeOverlaysOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CGame__CompleteInit), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CGame_CompleteInit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_completeInitOriginal));
    },
    "Boot");
