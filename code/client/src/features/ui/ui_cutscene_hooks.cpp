#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "ui_hud_elements.h"
#include "ui_hud_scripting.h"

#include "sdk/patterns.h"
#include "sdk/wh/guimodule/c_cutscene_configuration.h"

#include <memory>
#include <string>

/**
 * Story cutscenes -- the ones a quest graph plays when the player walks into
 * its trigger -- take the player out of play: the actor goes into its
 * cutscene state, the world clock can be held and the quest's own layers load.
 * None of that belongs to a multiplayer session, so they stay off until a
 * script calls `Hud.setStoryCutscenesEnabled(true)`, and scripts get
 * `storyCutsceneSuppressed` instead.
 *
 * Every one of them enters through `C_CutscenePlayer::EnqueueCutscene`, whose
 * only caller is the EnqueueCutscene port of the graph's `C_CutsceneHandler`.
 * Declining the request there leaves nothing to undo: the handler stays idle
 * and nothing about the player, the clock or the world was touched yet. A
 * quest's fades and time skips come this way too; the fast travel, sleep and
 * wait the player starts do not.
 */
namespace KCDC::Features::UI {
    namespace {
        using EnqueueCutscene_t = void(__fastcall *)(void *, const std::shared_ptr<SDK::wh::guimodule::C_CutsceneConfiguration> *);

        EnqueueCutscene_t g_enqueueCutsceneOriginal = nullptr;

        void __fastcall EnqueueCutscene_Hook(void *player, const std::shared_ptr<SDK::wh::guimodule::C_CutsceneConfiguration> *configuration) {
            // A name the table does not know is a request the player drops by
            // itself, so it is no cutscene to report.
            if (HudElements::Get().IsStoryCutscenesEnabled() || !(*configuration)->HasCutscene()) {
                g_enqueueCutsceneOriginal(player, configuration);
                return;
            }

            const SDK::CryStringT<char> &name = (*configuration)->GetName();
            const std::string cutscene        = name.m_pData != nullptr ? name.m_pData : "";
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Suppressed story cutscene '{}'", cutscene);
            KCDC::Scripting::Events::StoryCutsceneSuppressed(cutscene);
        }
    } // namespace
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CutscenePlayer__EnqueueCutscene), reinterpret_cast<LPVOID>(&KCDC::Features::UI::EnqueueCutscene_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_enqueueCutsceneOriginal));
    },
    "StoryCutscenes");
