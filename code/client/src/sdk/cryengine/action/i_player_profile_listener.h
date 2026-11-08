#pragma once

#include "i_player_profile.h"

namespace SDK {
    /**
     * Told when the current profile is loaded and when it is about to be
     * saved, so a system keeps its settings in the profile without owning a
     * file of its own. `C_UISettings` (base at +0x58) and `C_Keybinds` (base
     * at +0x8) are the game's two, and every option in the settings menu
     * reaches `attributes.xml` through one of them.
     *
     * Slot order confirmed on
     * `??_7C_UISettings@guimodule@wh@@6B@_1`: the destructor, then
     * SaveToProfile (0x181A71808 thunk), then LoadFromProfile (0x180A6A4D8
     * thunk). RTTI spells it `.?AVIPlayerProfileListener@@`, a class.
     */
    class IPlayerProfileListener {
      public:
        virtual ~IPlayerProfileListener()                                                       = default; // +0x00
        virtual void SaveToProfile(IPlayerProfile *profile, bool online, unsigned int reason)   = 0;       // +0x08
        virtual void LoadFromProfile(IPlayerProfile *profile, bool online, unsigned int reason) = 0;       // +0x10
    };
} // namespace SDK
