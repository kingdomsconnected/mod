#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include <logging/logger.h>

#include "native_menu.h"

#include "sdk/cryengine/scaleform/i_flash_player.h"
#include "sdk/cryengine/scaleform/s_flash_var_value.h"
#include "sdk/patterns.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"
#include "sdk/wh/guimodule/c_ui_menu.h"
#include "core/application.h"
#include "features/world/world_service.h"

#include <cstdint>
#include <cstring>

// Keep native input ownership while CEF replaces the front-end content.

namespace KCDC::Core {
    namespace {
        constexpr const char *kMovieRootVisible = "_root._visible";
        constexpr const char *kDisconnectButton = "KCDC.Disconnect";

        using CUIMenuEnterStateFn                = void(__fastcall *)(void *, std::uint8_t);
        CUIMenuEnterStateFn g_enterStateOriginal = nullptr;

        using CUIMenuShowRootMainFn                  = void(__fastcall *)(void *);
        CUIMenuShowRootMainFn g_showRootMainOriginal = nullptr;

        using EmitBasicButtonFn                     = bool(__fastcall *)(void *, const SDK::CryStringT<char> &, const char *const &, const int &, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, const bool &);
        EmitBasicButtonFn g_emitBasicButtonOriginal = nullptr;

        using OnFlashButtonFn                   = void(__fastcall *)(void *, SDK::CryStringT<char> *);
        OnFlashButtonFn g_onFlashButtonOriginal = nullptr;

        SDK::wh::guimodule::C_UIMenu *GetMenu() {
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            auto *gui         = environment != nullptr ? environment->GetGUIModule() : nullptr;
            return gui != nullptr ? gui->GetMenu() : nullptr;
        }

        bool IsSinglePlayerButton(const char *name) {
            if (name == nullptr) {
                return false;
            }

            constexpr SDK::wh::guimodule::E_ButtonId::Type buttons[] = {SDK::wh::guimodule::E_ButtonId::Type::Continue, SDK::wh::guimodule::E_ButtonId::Type::TryAgain, SDK::wh::guimodule::E_ButtonId::Type::NewGame, SDK::wh::guimodule::E_ButtonId::Type::NewGameDebug,
                SDK::wh::guimodule::E_ButtonId::Type::NewGameDebugTrosecko, SDK::wh::guimodule::E_ButtonId::Type::NewGameDebugKlaster, SDK::wh::guimodule::E_ButtonId::Type::SaveGame, SDK::wh::guimodule::E_ButtonId::Type::LoadGame, SDK::wh::guimodule::E_ButtonId::Type::SaveExit,
                SDK::wh::guimodule::E_ButtonId::Type::SaveReset};
            for (const auto button : buttons) {
                if (std::strcmp(name, SDK::wh::guimodule::ToString(button)) == 0) {
                    return true;
                }
            }
            return false;
        }

        bool SuppressButton(const char *name) {
            return KCDC::Features::World::WorldService::IsSessionActive() && IsSinglePlayerButton(name);
        }

        // AddButton must still destroy its native callback and string temporaries.
        bool __fastcall EmitBasicButton(void *menu, const SDK::CryStringT<char> &function, const char *const &id, const int &container, const SDK::CryStringT<char> &text, const SDK::CryStringT<char> &tooltip, const bool &disabled) {
            if (SuppressButton(id)) {
                return true;
            }
            auto *nativeMenu = GetMenu();
            if (KCDC::Features::World::WorldService::IsSessionActive() && nativeMenu != nullptr && menu == nativeMenu && (nativeMenu->GetPage() == SDK::wh::guimodule::E_MenuPage::Type::RootIngame || nativeMenu->GetPage() == SDK::wh::guimodule::E_MenuPage::Type::RootPause)
                && id != nullptr && std::strcmp(id, SDK::wh::guimodule::ToString(SDK::wh::guimodule::E_ButtonId::Type::Exit)) == 0) {
                const SDK::ScopedCryString label("@ui_SaveResetGame");
                const SDK::ScopedCryString hint("");
                const bool disconnectDisabled = false;
                g_emitBasicButtonOriginal(menu, function, kDisconnectButton, container, label.Get(), hint.Get(), disconnectDisabled);
            }
            return g_emitBasicButtonOriginal(menu, function, id, container, text, tooltip, disabled);
        }

        void __fastcall OnFlashButton(void *menu, SDK::CryStringT<char> *name) {
            if (name->m_pData != nullptr && std::strcmp(name->m_pData, kDisconnectButton) == 0) {
                if (gApplication != nullptr && KCDC::Features::World::WorldService::IsSessionActive()) {
                    gApplication->RequestServerDisconnect();
                }
                name->Assign("");
            }
            // Empty names skip dispatch; the original still releases the string.
            if (SuppressButton(name->m_pData)) {
                name->Assign("");
            }
            g_onFlashButtonOriginal(menu, name);
        }

        void __fastcall C_UIMenu_EnterState_Hook(void *menuSubObject, std::uint8_t context) {
            g_enterStateOriginal(menuSubObject, context);

            if (context == SDK::wh::guimodule::C_UIMenu::eMenuContext_FrontEnd) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Game front end is up");
            }
            if (auto *menu = GetMenu()) {
                if (auto *player = menu->GetFlashPlayer()) {
                    // Menu.gfx is shared with Escape. Restore its root when
                    // leaving the CEF front end for a native menu context.
                    player->SetVariable(kMovieRootVisible, SDK::SFlashVarValue::Bool(!menu->IsFrontEndContext()));
                }
            }
        }

        // Suppress front-end composition; in-game roots use separate builders.
        void __fastcall C_UIMenu_ShowRootMain_Hook(void *) {}
    } // namespace

    namespace NativeMenu {
        void HideFrontEndContent() {
            auto *menu = GetMenu();
            if (menu == nullptr || !menu->IsFrontEndContext()) {
                return;
            }

            if (auto *player = menu->GetFlashPlayer()) {
                player->SetVariable(kMovieRootVisible, SDK::SFlashVarValue::Bool(false));
            }
        }
    } // namespace NativeMenu
} // namespace KCDC::Core

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenu__EnterState), reinterpret_cast<LPVOID>(&KCDC::Core::C_UIMenu_EnterState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_enterStateOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenu__ShowRootMain), reinterpret_cast<LPVOID>(&KCDC::Core::C_UIMenu_ShowRootMain_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::g_showRootMainOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenuBase__EmitBasicButton), reinterpret_cast<LPVOID>(&KCDC::Core::EmitBasicButton), reinterpret_cast<LPVOID *>(&KCDC::Core::g_emitBasicButtonOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMenu__OnFlashButton), reinterpret_cast<LPVOID>(&KCDC::Core::OnFlashButton), reinterpret_cast<LPVOID *>(&KCDC::Core::g_onFlashButtonOriginal));
    },
    "NativeMenu");
