#pragma once

#include "s_equippable_item_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One `<Armor>` row; `<Helmet>` and `<Hood>` rows derive from it. Vtable
     * 0x183A4F418, allocated at 0x118 bytes (0x180754A5C).
     *
     * Reflection block `wh::entitymodule::Armor` (0x180FC9C78): `DefenseStab`,
     * `DefenseSlash`, `DefenseSmash` through setters, the slash one writing
     * +0xF4 (0x181110950); `StrReq` +0xFC and `Noise` +0x100 through the
     * member-offset helper, both as floats
     * (`property_wrapper<member_object_ptr, float S_ArmorItemClass::*>`,
     * 0x180FC9BB4) -- unlike a weapon's, an armor's strength requirement is
     * a float; `MaxStatus` and `FactionId` close the row. The eight bytes at
     * 0xE8, past the equippable base, are not reflected.
     * `C_RPGCombat::CalculateItemArmorDefense` (0x180686758) multiplies
     * +0xF0/+0xF4/+0xF8 by the condition multiplier, which is what the hit
     * calculation sums over the pieces the victim wears.
     */
    struct S_ArmorItemClass: S_EquippableItemClass {
        float GetDefenseStab() const {
            return m_defenseStab;
        }
        void SetDefenseStab(float value) {
            m_defenseStab = value;
        }
        float GetDefenseSlash() const {
            return m_defenseSlash;
        }
        void SetDefenseSlash(float value) {
            m_defenseSlash = value;
        }
        float GetDefenseSmash() const {
            return m_defenseSmash;
        }
        void SetDefenseSmash(float value) {
            m_defenseSmash = value;
        }
        float GetStrengthRequirement() const {
            return m_strReq;
        }
        void SetStrengthRequirement(float value) {
            m_strReq = value;
        }
        float GetNoise() const {
            return m_noise;
        }
        void SetNoise(float value) {
            m_noise = value;
        }

      private:
        std::byte m_opaque00E8[0x8];
        float m_defenseStab;   // 0xF0
        float m_defenseSlash;  // 0xF4
        float m_defenseSmash;  // 0xF8
        float m_strReq;        // 0xFC
        float m_noise;         // 0x100
        std::byte m_opaque0104[0x14];

        static void AssertLayout() {
            static_assert(offsetof(S_ArmorItemClass, m_defenseStab) == 0xF0, "S_ArmorItemClass::m_defenseStab offset mismatch");
            static_assert(offsetof(S_ArmorItemClass, m_defenseSlash) == 0xF4, "S_ArmorItemClass::m_defenseSlash offset mismatch");
            static_assert(offsetof(S_ArmorItemClass, m_defenseSmash) == 0xF8, "S_ArmorItemClass::m_defenseSmash offset mismatch");
            static_assert(offsetof(S_ArmorItemClass, m_strReq) == 0xFC, "S_ArmorItemClass::m_strReq offset mismatch");
            static_assert(offsetof(S_ArmorItemClass, m_noise) == 0x100, "S_ArmorItemClass::m_noise offset mismatch");
        }
    };
    static_assert(sizeof(S_ArmorItemClass) == 0x118, "S_ArmorItemClass size mismatch");
} // namespace SDK::wh::entitymodule
