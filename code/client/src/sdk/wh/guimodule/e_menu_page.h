#pragma once

#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The page a `C_UIMenu` is showing, as the game's own reflected
     * `E_MenuPage`.
     *
     * Stored at `C_UIMenu + 0xA1` and used to key the `menu_pages` row that
     * supplies the page heading and its button layout.
     */
    struct E_MenuPage {
        enum class Type : std::uint8_t {
            None                   = 0,
            RootMain               = 1,
            RootIngame             = 2,
            RootPause              = 3,
            SelectPlaylineNew      = 4,
            NewGameMode            = 5,
            NewGameModeDebug       = 6,
            SelectPlaylineLoad     = 7,
            LoadGame               = 8,
            SaveGame               = 9,
            Settings               = 10,
            GameSettings           = 11,
            GraphicSettings        = 12,
            AdvanceGraphicSettings = 13,
            SoundSettings          = 14,
            Controls               = 15,
            Keybinds               = 16,
            Credits                = 17,
            HelpOverlays           = 18,
            DLCList                = 19,
            RootPhotomode          = 20,
        };
    };
} // namespace SDK::wh::guimodule
