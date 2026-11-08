#pragma once

#include "s_weapon_equip_item_class.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * One `<Ammo>` row. Vtable 0x183BA6EE8, allocated at 0x140 bytes
     * (0x1815055D0). It derives from `S_WeaponEquipItemClass` and, as a
     * second base, `S_TypedItemClass` at +0xE8 (RTTI base descriptor
     * 0x1842FF740), so its own columns start at 0xF8.
     *
     * Reflection block `wh::entitymodule::Ammo` (0x180FCE30C): `AttackStab`,
     * `AttackSlash`, `AttackSmash` through setters, the slash one writing
     * +0xFC (0x181A744A0); `PowerMod` +0x104 and `RpgWeight` +0x108 through
     * the member-offset helper; quiver models and the broken class follow.
     */
    struct S_AmmoItemClass: S_WeaponEquipItemClass {
        float GetAttackStab() const {
            return m_attackStab;
        }
        void SetAttackStab(float value) {
            m_attackStab = value;
        }
        float GetAttackSlash() const {
            return m_attackSlash;
        }
        void SetAttackSlash(float value) {
            m_attackSlash = value;
        }
        float GetAttackSmash() const {
            return m_attackSmash;
        }
        void SetAttackSmash(float value) {
            m_attackSmash = value;
        }
        float GetPowerMod() const {
            return m_powerMod;
        }
        void SetPowerMod(float value) {
            m_powerMod = value;
        }

      private:
        std::byte m_typedItemClass00E8[0x10]; // the S_TypedItemClass base and what follows it
        float m_attackStab;          // 0xF8
        float m_attackSlash;         // 0xFC
        float m_attackSmash;         // 0x100
        float m_powerMod;            // 0x104
        std::byte m_opaque0108[0x38];

        static void AssertLayout() {
            static_assert(offsetof(S_AmmoItemClass, m_attackStab) == 0xF8, "S_AmmoItemClass::m_attackStab offset mismatch");
            static_assert(offsetof(S_AmmoItemClass, m_attackSlash) == 0xFC, "S_AmmoItemClass::m_attackSlash offset mismatch");
            static_assert(offsetof(S_AmmoItemClass, m_attackSmash) == 0x100, "S_AmmoItemClass::m_attackSmash offset mismatch");
            static_assert(offsetof(S_AmmoItemClass, m_powerMod) == 0x104, "S_AmmoItemClass::m_powerMod offset mismatch");
        }
    };
    static_assert(sizeof(S_AmmoItemClass) == 0x140, "S_AmmoItemClass size mismatch");
} // namespace SDK::wh::entitymodule
