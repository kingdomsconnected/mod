#pragma once

#include "../../types.h"
#include "../i_ui_menu.h"
#include "c_ui_flash_base.h"
#include "c_ui_menu_confirmation.h"
#include "c_ui_save_load.h"
#include "c_ui_settings.h"
#include "e_button_id.h"
#include "e_menu_page.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /** The exact spelling the movie echoes back through `OnButton`. */
    const char *ToString(E_ButtonId::Type id);

    /** The exact spelling the movie echoes back through `OnConfirm`. */
    const char *ToString(E_ConfirmationId::Type id);

    /**
     * The root menu controller: the object behind Menu.gfx's `C_UIMenu` UI
     * element, and the `this` every page builder on the root menu runs on.
     *
     * A page is not data. Each `ShowXxxPage` calls SetPage and then one
     * AddButton per entry, and the movie only ever draws what those calls
     * emitted -- so what a page contains is decided here, in C++, and the
     * `menu_pages` / `menu_buttons` tables only dress it.
     */
    class C_UIMenu final : public C_UIFlashBase {
      public:
        ~C_UIMenu() override = default;

        /**
         * Raises a modal through the movie, with the heading and the two
         * button labels supplied rather than read off the `menu_confirmations`
         * row.
         *
         * The game's own `ShowConfirmation` takes two `std::function` handlers
         * by value, which a mod cannot build without matching a private STL
         * layout. Emitting the flash call raises the same modal; the answer is
         * taken by detouring `OnFlashConfirm`, where a click lands regardless
         * of who raised it.
         */
        void ShowConfirmationRaw(E_ConfirmationId::Type id, const char *heading, const char *confirmLabel, const char *cancelLabel);

        /**
         * The save/load page, which the menu owns by value and nothing hands
         * out. Every way of starting a level goes through it.
         */
        C_UISaveLoad *GetSaveLoad() {
            return &m_saveLoad;
        }

        /** The settings pages, owned by value like the save/load page. */
        C_UISettings *GetSettings() {
            return &m_settings;
        }

        E_MenuPage::Type GetPage() const {
            return m_page;
        }

        /**
         * The menu's state machine, which the game dispatches through rather
         * than through this class. The sub-object is not a base here because
         * the prefix in front of it is modelled as bytes.
         */
        I_UIMenu *GetStateMachine() {
            return reinterpret_cast<I_UIMenu *>(reinterpret_cast<std::byte *>(this) + 0x58);
        }

        /**
         * Which menu the state machine is in, as `EnterState` (0x180C04B38)
         * stamps it into `m_context`.
         *
         * Each value is pinned by the root page that branch ends in:
         * `FrontEnd` and `GameOver` both reach `ShowRootMain` (0x180F6820C,
         * `SetPage(E_MenuPage::RootMain)`), `InGame` reaches `ShowRootIngame`
         * (0x1805598CC, `RootIngame`), `Pause` reaches `ShowRootPause`
         * (0x182BAA95C, `RootPause`) and `Photomode` reaches 0x181F8DE50
         * (`RootPhotomode`).
         *
         * `GameOver` is the front end as a death reaches it: `OnPictureHided`
         * (0x182BBA1A0) ends in `EnterState(4)`, the last step of the death
         * lifecycle. It shares the front-end branch with `FrontEnd` and
         * differs only in what `ShowRootMain` composes.
         */
        enum EMenuContext : std::uint8_t {
            eMenuContext_FrontEnd  = 1,
            eMenuContext_InGame    = 2,
            eMenuContext_Pause     = 3,
            eMenuContext_GameOver  = 4,
            eMenuContext_Photomode = 5,
        };

        /**
         * True while any menu state is up -- the escape menu, photo mode or
         * the front end. `LeaveState` (0x180C04780) zeroes the context on
         * every branch. The tab pages (map, codex, inventory) are not menu
         * states: they are the Apse, which holds `E_UIState::Apse` instead.
         */
        bool IsOpen() const {
            return m_context != 0;
        }

        /** True once the game has entered its own front-end menu state. */
        bool IsFrontEndContext() const {
            return m_context == eMenuContext_FrontEnd;
        }

        // Layout, not API: kept in one access section so the named bytes
        // cannot be reordered away from the offsets asserted below. The
        // C_UIFlashBase prefix occupies the first 0x58.
        std::byte m_padding0058[0x48];

        // Colour scheme, and the context flag ShowRootMain reads: GameOver
        // makes Continue read as TryAgain and drops NewGame entirely.
        EMenuContext m_context; // 0xA0

        E_MenuPage::Type m_page; // 0xA1

        std::byte m_padding00A2[0xE];
        C_UISaveLoad m_saveLoad; // 0xB0
        C_UISettings m_settings; // 0x1D8
        std::byte m_padding0668[0x30];
    };

    static_assert(offsetof(C_UIMenu, m_context) == 0xA0, "C_UIMenu context offset mismatch");
    static_assert(offsetof(C_UIMenu, m_page) == 0xA1, "C_UIMenu page offset mismatch");
    static_assert(offsetof(C_UIMenu, m_saveLoad) == 0xB0, "C_UIMenu save/load page offset mismatch");
    static_assert(offsetof(C_UIMenu, m_settings) == 0x1D8, "C_UIMenu settings pages offset mismatch");

    // The allocation C_UIMenu's RTTR constructor wrapper makes.
    static_assert(sizeof(C_UIMenu) == 0x698, "C_UIMenu size mismatch");
} // namespace SDK::wh::guimodule
