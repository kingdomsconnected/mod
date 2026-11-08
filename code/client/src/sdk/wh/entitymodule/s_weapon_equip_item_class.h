#pragma once

#include "s_equippable_item_class.h"

namespace SDK::wh::entitymodule {
    /**
     * What the weapon slots take: the base of `S_WeaponItemClass` and
     * `S_AmmoItemClass` (RTTI base-class arrays 0x184309A50, 0x1842F8D80), and
     * the class `IsOfClass(E_ItemClassId::WeaponEquip)` answers for. It adds
     * no data: a bare row is allocated at 0xE8 bytes and built by the
     * equippable constructor before its own vftable is stored (0x182A4053A).
     */
    struct S_WeaponEquipItemClass: S_EquippableItemClass {};

    static_assert(sizeof(S_WeaponEquipItemClass) == 0xE8, "S_WeaponEquipItemClass size mismatch");
} // namespace SDK::wh::entitymodule
