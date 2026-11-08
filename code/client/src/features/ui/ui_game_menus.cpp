#include <utils/safe_win32.h>

#include "ui_game_menus.h"

#include "features/character_creator/character_creator_service.h"
#include "sdk/patterns.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_ui_apse.h"

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

namespace KCDC::Features::UI {
    GameMenus &GameMenus::Get() {
        static GameMenus menus;
        return menus;
    }

    bool GameMenus::AllowsView(SDK::wh::framework::E_UIApseView::Type view) const {
        return IsViewEnabled(view) || (view == SDK::wh::framework::E_UIApseView::Type::Player && KCDC::Features::CharacterCreator::CharacterCreatorService::Get().IsOpen());
    }

    void GameMenus::Update() {
        if (!HasRestrictions()) {
            return;
        }
        SDK::wh::guimodule::C_UIApse *apse = SDK::wh::guimodule::C_UIApse::GetInstance();
        if (apse == nullptr || AllowsView(apse->GetRequestedView())) {
            return;
        }
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::framework::I_UIApse *interface   = environment != nullptr ? environment->GetUIApse() : nullptr;
        if (interface != nullptr) {
            interface->Hide(true);
        }
    }

    bool __fastcall GameMenus::ShowHook(SDK::wh::framework::I_UIApse *apse, SDK::wh::framework::E_UIApseView::Type view, SDK::wh::framework::S_UIApseShowParams *params) {
        GameMenus &menus = Get();
        if (!menus.AllowsView(view)) {
            // Show consumes the callable on both success and refusal.
            params->ReleaseCallback();
            return false;
        }
        return menus._showOriginal(apse, view, params);
    }

    void __fastcall GameMenus::MoveToViewHook(SDK::wh::guimodule::C_UIApseCamera *camera, SDK::wh::framework::E_UIApseView::Type view, bool animate) {
        GameMenus &menus = Get();
        if (menus.AllowsView(view)) {
            menus._moveToViewOriginal(camera, view, animate);
        }
    }

    void GameMenus::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.I_UIApse__Show), reinterpret_cast<LPVOID>(&ShowHook), reinterpret_cast<LPVOID *>(&Get()._showOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseCamera__MoveToView), reinterpret_cast<LPVOID>(&MoveToViewHook), reinterpret_cast<LPVOID *>(&Get()._moveToViewOriginal));
    }
} // namespace KCDC::Features::UI

static InitFunction init([]() { KCDC::Features::UI::GameMenus::Install(); }, "GameMenus");
