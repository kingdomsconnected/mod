#pragma once

#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * Identity of a menu entry, as the game's own reflected `E_ButtonId`.
     *
     * Closed and compiled in: `menu_buttons.xml` keys its rows on it, the C++
     * page builders name it directly, and the movie echoes the spelling back
     * through `OnButton`. A mod can reuse a value but cannot invent one.
     *
     * 94 values in all. Only those the root menu and the pages reachable from
     * it use are named here; the remaining 66 are settings rows.
     */
    struct E_ButtonId {
        enum class Type : std::uint8_t {
            Invalid              = 0,
            Resume               = 1,
            Apply                = 2,
            Reset                = 3,
            Back                 = 4,
            Continue             = 5,
            TryAgain             = 6,
            NewGame              = 7,
            NewGameDebug         = 8,
            NewGameDebugTrosecko = 9,
            NewGameDebugKlaster  = 10,
            SaveGame             = 11,
            LoadGame             = 12,
            PreviouslyKCD        = 13,
            Settings             = 14,
            HelpOverlays         = 15,
            DLC                  = 16,
            Credits              = 17,
            Exit                 = 18,
            SaveExit             = 19,
            SaveReset            = 20,
        };
    };
} // namespace SDK::wh::guimodule
