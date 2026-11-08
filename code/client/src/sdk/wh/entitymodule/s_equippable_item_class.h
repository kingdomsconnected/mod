#pragma once

#include "s_player_item_class.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Anything the equipment manager can wear: armor, weapons and ammunition.
     *
     * The 0x38 bytes it adds to `S_PlayerItemClass` are the wearing columns
     * (Visibility, Conspicuousness, Charisma, SocialClassId, WealthLevel,
     * MaxQuality, UiSound, Clothing, ...); none is needed yet, so they stay
     * opaque. Its constructor (0x180754B60) initialises them out to +0xE0, and
     * a bare row is allocated at 0xE8 bytes (0x182A1697A). Each derived class
     * places its first reflected column at +0xF0 (0x180FC9C78, 0x180FC8BD0).
     */
    struct S_EquippableItemClass: S_PlayerItemClass {
      private:
        std::byte m_opaque00B0[0x38];
    };

    static_assert(sizeof(S_EquippableItemClass) == 0xE8, "S_EquippableItemClass size mismatch");
} // namespace SDK::wh::entitymodule
