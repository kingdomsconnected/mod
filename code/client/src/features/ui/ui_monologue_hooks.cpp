#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "ui_hud_elements.h"
#include "ui_hud_scripting.h"

#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/dialogmodule/c_dialog_instance.h"
#include "sdk/wh/dialogmodule/c_dialog_manager.h"
#include "sdk/wh/dialogmodule/i_dialog.h"
#include "sdk/wh/entitymodule/c_actor_speak_man.h"
#include "sdk/wh/entitymodule/s_speak_data.h"

#include <cstdint>
#include <string>

/**
 * The local character's own monologue -- photo-mode comments, the sharpening
 * remark -- is spoken in Henry's voice and subtitled under his name, whoever
 * is playing. It is suppressed until a script calls
 * `Hud.setMonologueEnabled(true)`: the line still runs, so the dialog
 * instance advances, but without voice or subtitle, and scripts get
 * `monologueSuppressed` instead.
 *
 * A monologue line is one `C_DialogInstance::SpeakLine180954638` speaks for
 * the player's own dialog (`C_DialogManager::m_pPlayerDialog`) when it has a
 * single participant (I_Dialog vslot23). SpeakLine sets the subtitle and then
 * calls Speak on the same stack, so the line is bracketed here.
 */
namespace KCDC::Features::UI {
    namespace {
        using SpeakLine_t = bool(__fastcall *)(SDK::wh::dialogmodule::C_DialogInstance *, void *, void *);
        using SetText_t   = std::int64_t(__fastcall *)(void *, const SDK::CryStringT<char> *, const SDK::CryStringT<char> *, bool, bool, bool);
        using Speak_t     = void(__fastcall *)(SDK::wh::entitymodule::C_ActorSpeakMan *, const SDK::wh::entitymodule::S_SpeakData *);

        SpeakLine_t g_speakLineOriginal = nullptr;
        SetText_t g_setTextOriginal     = nullptr;
        Speak_t g_speakOriginal         = nullptr;

        /** The line in the player's language, or the key itself when the table has none. */
        std::string Localized(const char *key) {
            SDK::ILocalizationManager *strings = SDK::ILocalizationManager::GetInstance();
            SDK::ScopedCryString localized("");
            const std::string label = std::string("@") + key;
            return strings != nullptr && strings->LocalizeLabel(label.c_str(), *localized.Out()) && localized.c_str()[0] != '\0' ? std::string(localized.c_str()) : std::string(key);
        }

        bool __fastcall SpeakLine_Hook(SDK::wh::dialogmodule::C_DialogInstance *instance, void *line, void *unknown) {
            SDK::wh::dialogmodule::C_DialogManager *manager = SDK::wh::dialogmodule::C_DialogManager::GetInstance();
            HudElements::Get().SetSpeakingMonologue(manager != nullptr && manager->GetPlayerDialog() == instance && instance->GetDialog()->HasSingleParticipant());
            const bool result = g_speakLineOriginal(instance, line, unknown);
            HudElements::Get().SetSpeakingMonologue(false);
            return result;
        }

        // Skipped rather than called with empty text: that takes SetText's
        // ClearSubtitles branch and wipes a line somebody else is saying.
        std::int64_t __fastcall SetText_Hook(void *subtitles, const SDK::CryStringT<char> *text, const SDK::CryStringT<char> *speaker, bool unknown, bool force, bool localize) {
            if (HudElements::Get().IsMonologueSuppressed()) {
                return 0;
            }
            return g_setTextOriginal(subtitles, text, speaker, unknown, force, localize);
        }

        void __fastcall Speak_Hook(SDK::wh::entitymodule::C_ActorSpeakMan *speakMan, const SDK::wh::entitymodule::S_SpeakData *data) {
            if (!HudElements::Get().IsMonologueSuppressed()) {
                g_speakOriginal(speakMan, data);
                return;
            }

            const char *textKey = data->text.m_pData != nullptr ? data->text.m_pData : "";
            KCDC::Scripting::Events::MonologueSuppressed(Localized(textKey), textKey);

            // Borrowed copy: `data` outlives the call and Speak takes every
            // string through the engine's own assignment. An empty sound skips
            // the voice (`C_ActorSpeakMan::StartVoice180956360`) while the end
            // timer still runs, so the instance advances as it would have.
            const SDK::ScopedCryString silence("");
            SDK::wh::entitymodule::S_SpeakData muted = *data;
            muted.soundEvent                         = silence.Get();
            g_speakOriginal(speakMan, &muted);
        }
    } // namespace
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DialogInstance__SpeakLine), reinterpret_cast<LPVOID>(&KCDC::Features::UI::SpeakLine_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_speakLineOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UISubtitles__SetText), reinterpret_cast<LPVOID>(&KCDC::Features::UI::SetText_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_setTextOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorSpeakMan__Speak), reinterpret_cast<LPVOID>(&KCDC::Features::UI::Speak_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_speakOriginal));
    },
    "Monologue");
