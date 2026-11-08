#pragma once

#include "s_weapon_item_class.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * One `<MeleeWeapon>` row. Vtable 0x183A4E390, allocated at 0x150 bytes
     * (0x18075361C). Reflection block `wh::entitymodule::MeleeWeapon`
     * (0x181347D74) registers `Attack` first, then `AttackModStab`,
     * `AttackModSlash`, `AttackModSmash` through setters that write +0x13C,
     * +0x140 and +0x144 (0x1812F0090, 0x1812F0080, 0x1812F0070), so `Attack`
     * is the float before them at +0x138. `SharpeningAnimSuffix` closes the
     * row. The hit calculation reads these through the item's descriptor on
     * every hit, so a write here is live for the next swing.
     */
    struct S_MeleeWeaponItemClass: S_WeaponItemClass {
        float GetAttack() const {
            return m_attack;
        }
        void SetAttack(float value) {
            m_attack = value;
        }
        float GetAttackModStab() const {
            return m_attackModStab;
        }
        void SetAttackModStab(float value) {
            m_attackModStab = value;
        }
        float GetAttackModSlash() const {
            return m_attackModSlash;
        }
        void SetAttackModSlash(float value) {
            m_attackModSlash = value;
        }
        float GetAttackModSmash() const {
            return m_attackModSmash;
        }
        void SetAttackModSmash(float value) {
            m_attackModSmash = value;
        }

      private:
        float m_attack;              // 0x138
        float m_attackModStab;       // 0x13C
        float m_attackModSlash;      // 0x140
        float m_attackModSmash;      // 0x144
        std::byte m_opaque0148[0x8]; // SharpeningAnimSuffix

        static void AssertLayout() {
            static_assert(offsetof(S_MeleeWeaponItemClass, m_attack) == 0x138, "S_MeleeWeaponItemClass::m_attack offset mismatch");
            static_assert(offsetof(S_MeleeWeaponItemClass, m_attackModStab) == 0x13C, "S_MeleeWeaponItemClass::m_attackModStab offset mismatch");
            static_assert(offsetof(S_MeleeWeaponItemClass, m_attackModSlash) == 0x140, "S_MeleeWeaponItemClass::m_attackModSlash offset mismatch");
            static_assert(offsetof(S_MeleeWeaponItemClass, m_attackModSmash) == 0x144, "S_MeleeWeaponItemClass::m_attackModSmash offset mismatch");
        }
    };
    static_assert(sizeof(S_MeleeWeaponItemClass) == 0x150, "S_MeleeWeaponItemClass size mismatch");
} // namespace SDK::wh::entitymodule
