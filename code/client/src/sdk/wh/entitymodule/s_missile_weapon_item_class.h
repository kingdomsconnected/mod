#pragma once

#include "s_weapon_item_class.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * One `<MissileWeapon>` row. Vtable 0x183A4D890, allocated at 0x158 bytes
     * (0x180752A58). Reflection block `wh::entitymodule::MissileWeapon`
     * (0x18153C598): `Power` +0x138 and `LimbResistance` +0x13C through the
     * member-offset helper, then `OldFormula`, `SoundSwitch` and
     * `FireSoundMultiplicator` (+0x150). `C_RPGCombat::CalculateProjectileLaunchSpeed`
     * (0x180726A94) multiplies by +0x138 and reads +0x13C.
     */
    struct S_MissileWeaponItemClass: S_WeaponItemClass {
        float GetPower() const {
            return m_power;
        }
        void SetPower(float value) {
            m_power = value;
        }
        float GetLimbResistance() const {
            return m_limbResistance;
        }
        void SetLimbResistance(float value) {
            m_limbResistance = value;
        }

      private:
        float m_power;          // 0x138
        float m_limbResistance; // 0x13C
        std::byte m_opaque0140[0x18];

        static void AssertLayout() {
            static_assert(offsetof(S_MissileWeaponItemClass, m_power) == 0x138, "S_MissileWeaponItemClass::m_power offset mismatch");
            static_assert(offsetof(S_MissileWeaponItemClass, m_limbResistance) == 0x13C, "S_MissileWeaponItemClass::m_limbResistance offset mismatch");
        }
    };
    static_assert(sizeof(S_MissileWeaponItemClass) == 0x158, "S_MissileWeaponItemClass size mismatch");
} // namespace SDK::wh::entitymodule
