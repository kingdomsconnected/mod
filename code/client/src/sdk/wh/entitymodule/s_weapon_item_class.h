#pragma once

#include "s_weapon_equip_item_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The `<MeleeWeapon>` / `<MissileWeapon>` columns both weapon kinds share.
     *
     * Offsets from the reflection block `wh::entitymodule::Weapon`
     * (0x180FC8BD0): `Class` and `SubClass` are the `weapon_class.xml` and
     * sub-class ids passed to the member-offset helper as 0xF0 and 0xF4,
     * `Defense` is a float registered through the float-property helper with
     * offset 0x110, and `MaxStatus`, `StrReq`, `AgiReq` follow at 0x114,
     * 0x118, 0x11C. The two requirements are `unsigned int` members: the
     * block registers them through
     * `property_wrapper<member_object_ptr, unsigned int S_WeaponItemClass::*>`
     * (0x180FC94F0). `IsBreakable`, `BrokenItemClassId`, `HolsterModel`
     * (0x128) and `HolsterMaterial` (0x130) fill the rest; both derived
     * classes start their own fields at 0x138. The eight bytes at 0xE8, past
     * `S_WeaponEquipItemClass`, are not reflected.
     */
    struct S_WeaponItemClass: S_WeaponEquipItemClass {
        std::int32_t GetWeaponClassId() const {
            return m_class;
        }
        float GetDefense() const {
            return m_defense;
        }
        void SetDefense(float value) {
            m_defense = value;
        }
        std::uint32_t GetStrengthRequirement() const {
            return m_strReq;
        }
        void SetStrengthRequirement(std::uint32_t value) {
            m_strReq = value;
        }
        std::uint32_t GetAgilityRequirement() const {
            return m_agiReq;
        }
        void SetAgilityRequirement(std::uint32_t value) {
            m_agiReq = value;
        }

      private:
        std::byte m_opaque00E8[0x8];
        std::int32_t m_class;     // 0xF0
        std::int32_t m_subClass;  // 0xF4
        std::byte m_opaque00F8[0x18];
        float m_defense;          // 0x110
        std::int32_t m_maxStatus; // 0x114
        std::uint32_t m_strReq;   // 0x118
        std::uint32_t m_agiReq;   // 0x11C
        std::byte m_opaque0120[0x18];

        static void AssertLayout() {
            static_assert(offsetof(S_WeaponItemClass, m_class) == 0xF0, "S_WeaponItemClass::m_class offset mismatch");
            static_assert(offsetof(S_WeaponItemClass, m_subClass) == 0xF4, "S_WeaponItemClass::m_subClass offset mismatch");
            static_assert(offsetof(S_WeaponItemClass, m_defense) == 0x110, "S_WeaponItemClass::m_defense offset mismatch");
            static_assert(offsetof(S_WeaponItemClass, m_maxStatus) == 0x114, "S_WeaponItemClass::m_maxStatus offset mismatch");
            static_assert(offsetof(S_WeaponItemClass, m_strReq) == 0x118, "S_WeaponItemClass::m_strReq offset mismatch");
            static_assert(offsetof(S_WeaponItemClass, m_agiReq) == 0x11C, "S_WeaponItemClass::m_agiReq offset mismatch");
        }
    };
    static_assert(sizeof(S_WeaponItemClass) == 0x138, "S_WeaponItemClass size mismatch");
} // namespace SDK::wh::entitymodule
