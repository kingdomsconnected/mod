#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/guimodule/c_ui_menu.h"
#include "sdk/wh/guimodule/c_ui_settings.h"

#include <stdexcept>

// The voice rows on the game's Sound settings page, and the streamed-asset
// rows on its Game page. ShowPage and the interactive handlers are shared by
// every page, and MinHook takes one detour per function, so both features are
// dispatched from here. Each detour only recognises its moment and hands it
// on; the originals always run, since the game's handlers already ignore row
// ids they do not know and still have to release the by-value id string they
// were given.

namespace KCDC::Features::Social {
    namespace {
        using ShowSoundSettingsPage_t                           = void(__fastcall *)(SDK::wh::guimodule::C_UISettings *);
        ShowSoundSettingsPage_t g_showSoundSettingsPageOriginal = nullptr;

        using ShowGameSettingsPage_t                          = void(__fastcall *)(SDK::wh::guimodule::C_UISettings *);
        ShowGameSettingsPage_t g_showGameSettingsPageOriginal = nullptr;

        using ShowPage_t              = void(__fastcall *)(SDK::wh::guimodule::C_UIMenu *);
        ShowPage_t g_showPageOriginal = nullptr;

        // The id is by value in the native signature, so it arrives as a pointer
        // to the caller's temporary; the value rides in XMM2.
        using OnInteractiveValue_t                        = void(__fastcall *)(SDK::wh::guimodule::C_UISettings *, SDK::CryStringT<char> *, float);
        OnInteractiveValue_t g_onInteractiveValueOriginal = nullptr;

        using OnInteractiveChoice_t                         = void(__fastcall *)(SDK::wh::guimodule::C_UISettings *, SDK::CryStringT<char> *, unsigned int);
        OnInteractiveChoice_t g_onInteractiveChoiceOriginal = nullptr;

        void __fastcall ShowSoundSettingsPage_Hook(SDK::wh::guimodule::C_UISettings *settings) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetVoiceService().GetSettings().ArmSoundPage();
            }
            g_showSoundSettingsPageOriginal(settings);
        }

        void __fastcall ShowGameSettingsPage_Hook(SDK::wh::guimodule::C_UISettings *settings) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetStreamedAssetService().GetSettings().ArmGamePage();
            }
            g_showGameSettingsPageOriginal(settings);
        }

        // Before the original: rows emitted after ShowPage would miss the
        // layout and navigation the movie builds when the page is shown.
        void __fastcall ShowPage_Hook(SDK::wh::guimodule::C_UIMenu *menu) {
            if (KCDC::Core::gApplication != nullptr && menu != nullptr) {
                KCDC::Core::gApplication->GetVoiceService().GetSettings().OnShowPage(*menu);
                KCDC::Core::gApplication->GetStreamedAssetService().GetSettings().OnShowPage(*menu);
            }
            g_showPageOriginal(menu);
        }

        void __fastcall OnInteractiveValue_Hook(SDK::wh::guimodule::C_UISettings *settings, SDK::CryStringT<char> *id, float value) {
            if (KCDC::Core::gApplication != nullptr && id != nullptr) {
                KCDC::Core::gApplication->GetVoiceService().GetSettings().OnValue(id->m_pData, value);
            }
            g_onInteractiveValueOriginal(settings, id, value);
        }

        void __fastcall OnInteractiveChoice_Hook(SDK::wh::guimodule::C_UISettings *settings, SDK::CryStringT<char> *id, unsigned int index) {
            if (KCDC::Core::gApplication != nullptr && id != nullptr) {
                if (!KCDC::Core::gApplication->GetVoiceService().GetSettings().OnChoice(id->m_pData, index)) {
                    KCDC::Core::gApplication->GetStreamedAssetService().GetSettings().OnChoice(id->m_pData, index);
                }
            }
            g_onInteractiveChoiceOriginal(settings, id, index);
        }
    } // namespace
} // namespace KCDC::Features::Social

static InitFunction init(
    []() {
        const MH_STATUS page =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UISettings__ShowSoundSettingsPage), reinterpret_cast<LPVOID>(&KCDC::Features::Social::ShowSoundSettingsPage_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::g_showSoundSettingsPageOriginal));
        const MH_STATUS game = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UISettings__ShowGameSettingsPage), reinterpret_cast<LPVOID>(&KCDC::Features::Social::ShowGameSettingsPage_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::g_showGameSettingsPageOriginal));
        const MH_STATUS show  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenu__ShowPage), reinterpret_cast<LPVOID>(&KCDC::Features::Social::ShowPage_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::g_showPageOriginal));
        const MH_STATUS value = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UISettings__OnInteractiveValue), reinterpret_cast<LPVOID>(&KCDC::Features::Social::OnInteractiveValue_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::g_onInteractiveValueOriginal));
        const MH_STATUS choice =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UISettings__OnInteractiveChoice), reinterpret_cast<LPVOID>(&KCDC::Features::Social::OnInteractiveChoice_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Social::g_onInteractiveChoiceOriginal));
        if (page != MH_OK || game != MH_OK || show != MH_OK || value != MH_OK || choice != MH_OK) {
            throw std::runtime_error("Could not install the settings page hooks");
        }
    },
    "VoiceSettings");
