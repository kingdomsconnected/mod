#pragma once

#include "../../cry/gameplatform/i_service_listener.h"
#include "../../cryengine/scaleform/i_ui_element_event_listener.h"
#include "../../cryengine/system/i_system_event_listener.h"
#include "../framework/c_base_module.h"
#include "c_fader_controller.h"
#include "c_ui_action_hint_manager.h"
#include "c_ui_base.h"
#include "c_ui_overlay.h"
#include "c_ui_sound_controller.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace SDK::wh::guimodule {
    class C_UIApse;
    class C_UILoadingScreen;
    class C_UIMenu;

    /** Typed prefix, page ownership and fader ownership of the GUI module. */
    class C_GUIModule
        : public framework::C_BaseModule
        , public ISystemEventListener
        , public Cry::GamePlatform::IServiceListener {
      public:
        /**
         * The root menu, found by walking the pages the module owns.
         *
         * The game reaches its own pages through 32 GetPage<T> instantiations
         * over this same vector. They share a prologue byte for byte, so none
         * is reachable by signature and the vtable is matched instead.
         */
        C_UIMenu *GetMenu() const;

        /** The page that owns the main-menu backdrop. */
        C_UILoadingScreen *GetLoadingScreen() const;

        /** The tabbed pause screen, which owns the map page. */
        C_UIApse *GetApse() const;

        /** The full-screen overlay page, which owns the game's own screen tint. */
        C_UIOverlay *GetOverlay() const;

        /** GUI-owned common audio/button event listener; never delete it. */
        SDK::IUIElementEventListener *GetCommonEventHandler() const {
            return m_pCommonEventHandler;
        }

        /** The presenter behind the HUD's "press X" hints, created by the module's constructor (0x18178958B) and owned by it. */
        C_UIActionHintManager *GetActionHintManager() const {
            return m_pActionHintManager;
        }

        C_FaderController *GetFaderController() const {
            return m_faderController.get();
        }

        void PlayUISound(E_UISound sound) const {
            if (m_pUISoundController != nullptr) {
                m_pUISoundController->Play(sound);
            }
        }

      private:
        /** The page whose primary vtable is `vtable`, or null. */
        C_UIBase *FindPage(std::uint64_t vtable) const;

        std::uint64_t m_state0020; // 0x20, zeroed by the constructor (0x181789268)
        SDK::IUIElementEventListener *m_pCommonEventHandler; // 0x28
        C_UIActionHintManager *m_pActionHintManager; // 0x30
        void *m_pBuffsManager; // 0x38, C_UIBuffsManager, a 0x68-byte object built by 0x1817894B8

        // Every page the module owns, and the range all 32 of the game's own
        // GetPage<T> instantiations walk.
        std::vector<std::shared_ptr<C_UIBase>> m_pages; // 0x40

        std::shared_ptr<C_FaderController> m_faderController; // 0x58
        void *m_vector0068[3]; // 0x68, zeroed by the constructor
        C_UISoundController180559ED0 *m_pUISoundController; // 0x80
        void *m_pQAMSelectionHandler; // 0x88, C_UIQAMSelectionHandler, built by 0x1817895D4
        std::uint64_t m_state0090;    // 0x90
        // C_ScriptBindUI, a CScriptableBase registering the UI script methods;
        // the constructor stores the script system at +0x60 and the system at
        // +0x68 of it before CScriptableBase::Init.
        std::byte m_scriptBind[0x70]; // 0x98
        std::byte m_state0108[0x10];  // 0x108, built by 0x181F76DE0
        bool m_state0118;             // 0x118
        std::byte m_padding0119[0x7];

        static void AssertLayout() {
            // Three pointers, so the module's own fields keep their offsets.
            // A build that turned MSVC's iterator debugging on would add a
            // fourth and slide everything after it.
            static_assert(sizeof(std::vector<std::shared_ptr<C_UIBase>>) == 0x18, "std::vector layout does not match the game's");
            static_assert(offsetof(C_GUIModule, m_pCommonEventHandler) == 0x28, "C_GUIModule::m_pCommonEventHandler offset mismatch");
            static_assert(offsetof(C_GUIModule, m_pActionHintManager) == 0x30, "C_GUIModule::m_pActionHintManager offset mismatch");
            static_assert(offsetof(C_GUIModule, m_pages) == 0x40, "C_GUIModule::m_pages offset mismatch");
            static_assert(offsetof(C_GUIModule, m_faderController) == 0x58, "C_GUIModule::m_faderController offset mismatch");
            static_assert(offsetof(C_GUIModule, m_pUISoundController) == 0x80, "C_GUIModule::m_pUISoundController offset mismatch");
            static_assert(offsetof(C_GUIModule, m_scriptBind) == 0x98, "C_GUIModule::m_scriptBind offset mismatch");
            static_assert(offsetof(C_GUIModule, m_state0118) == 0x118, "C_GUIModule::m_state0118 offset mismatch");
        }
    };

    static_assert(sizeof(C_GUIModule) == 0x120, "C_GUIModule size mismatch");
} // namespace SDK::wh::guimodule
