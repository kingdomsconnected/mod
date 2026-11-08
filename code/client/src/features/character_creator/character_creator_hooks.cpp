#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/character_creator/character_creator_service.h"
#include "features/ui/ui_game_menus.h"

#include "sdk/cryengine/action/s_view_params.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/framework/e_ui_apse_view.h"
#include "sdk/wh/framework/i_ui_apse.h"
#include "sdk/wh/guimodule/c_ui_apse.h"
#include "sdk/wh/guimodule/c_ui_apse_camera.h"
#include "sdk/wh/guimodule/c_ui_apse_character.h"

namespace KCDC::Features::CharacterCreator {
    namespace {
        using CopyForInventoryPreview_t                             = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, SDK::wh::entitymodule::C_Actor *);
        CopyForInventoryPreview_t g_copyForInventoryPreviewOriginal = nullptr;

        using ApseHide_t              = void(__fastcall *)(SDK::wh::framework::I_UIApse *, bool);
        ApseHide_t g_apseHideOriginal = nullptr;

        using ApseChangeView_t                    = void(__fastcall *)(SDK::wh::guimodule::C_UIApse *, SDK::wh::framework::E_UIApseView::Type, bool);
        ApseChangeView_t g_apseChangeViewOriginal = nullptr;

        using BindCharacterHub_t                      = void(__fastcall *)(SDK::wh::guimodule::C_UIApseCharacter *);
        BindCharacterHub_t g_bindCharacterHubOriginal = nullptr;

        using GetCameraParams_t                     = void(__fastcall *)(const SDK::wh::guimodule::C_UIApseCamera *, SDK::SViewParams *);
        GetCameraParams_t g_getCameraParamsOriginal = nullptr;

        /** The screen's camera handing over its pose; an open creator frames the figure instead of the panels. */
        void __fastcall GetCameraParams_Hook(const SDK::wh::guimodule::C_UIApseCamera *camera, SDK::SViewParams *params) {
            g_getCameraParamsOriginal(camera, params);
            CharacterCreatorService::Get().FrameStage(*params);
        }

        /** Every show of the character page binds its stage first; an open creator stands its own figure in the slot. */
        void __fastcall BindCharacterHub_Hook(SDK::wh::guimodule::C_UIApseCharacter *character) {
            g_bindCharacterHubOriginal(character);
            CharacterCreatorService::Get().OnStageBound(character);
        }

        /**
         * The page dressing its figure as the player. The copy always runs:
         * it builds the figure's inventory and the item map the page reads.
         * An open creator then finishes the figure its own way.
         */
        void __fastcall CopyForInventoryPreview_Hook(SDK::wh::entitymodule::C_Actor *dummy, SDK::wh::entitymodule::C_Actor *source) {
            g_copyForInventoryPreviewOriginal(dummy, source);
            CharacterCreatorService::Get().DressFigure(dummy);
        }

        /**
         * Anything closing the screen under an open creator other than the
         * creator itself: the close is made immediate, so the figure can be
         * put back the moment it returns rather than after the fade.
         */
        void __fastcall ApseHide_Hook(SDK::wh::framework::I_UIApse *apse, bool immediate) {
            CharacterCreatorService &creator = CharacterCreatorService::Get();
            const bool cancelled             = creator.ClaimsHide();
            g_apseHideOriginal(apse, cancelled || immediate);
            if (cancelled) {
                creator.OnApseCancelled();
            }
        }

        /** Every page change once the screen is up; a creator holds it on the player view. */
        void __fastcall ApseChangeView_Hook(SDK::wh::guimodule::C_UIApse *apse, SDK::wh::framework::E_UIApseView::Type view, bool immediate) {
            if (!CharacterCreatorService::Get().AllowsViewChange(view) || !KCDC::Features::UI::GameMenus::Get().AllowsView(view)) {
                return;
            }
            g_apseChangeViewOriginal(apse, view, immediate);
        }
    } // namespace
} // namespace KCDC::Features::CharacterCreator

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseCamera__GetCameraParams), reinterpret_cast<LPVOID>(&KCDC::Features::CharacterCreator::GetCameraParams_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::CharacterCreator::g_getCameraParamsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseCharacter__BindCharacterHub), reinterpret_cast<LPVOID>(&KCDC::Features::CharacterCreator::BindCharacterHub_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::CharacterCreator::g_bindCharacterHubOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__CopyForInventoryPreview), reinterpret_cast<LPVOID>(&KCDC::Features::CharacterCreator::CopyForInventoryPreview_Hook),
            reinterpret_cast<LPVOID *>(&KCDC::Features::CharacterCreator::g_copyForInventoryPreviewOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.I_UIApse__Hide), reinterpret_cast<LPVOID>(&KCDC::Features::CharacterCreator::ApseHide_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::CharacterCreator::g_apseHideOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApse__ChangeView), reinterpret_cast<LPVOID>(&KCDC::Features::CharacterCreator::ApseChangeView_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::CharacterCreator::g_apseChangeViewOriginal));
    },
    "CharacterCreator");
