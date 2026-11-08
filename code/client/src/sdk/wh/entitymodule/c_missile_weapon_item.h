#pragma once

#include "c_pickable_item.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Exact factory-initialized derived state of C_MissileWeaponItem. */
    struct MissileWeaponItemStateLayout {
        std::uint16_t m_state0000;
        std::uint8_t m_state0002;
        std::byte m_padding0003[0x1];
        std::uint32_t m_state0004;
    };

    static_assert(sizeof(MissileWeaponItemStateLayout) == 0x8,
                  "MissileWeaponItemStateLayout size mismatch");

    /**
     * Concrete ranged/projectile weapon item.
     *
     * C_MissileWeaponItem's primary table at 0x183B5AD28 contains exactly
     * the 130 entries inherited from C_PickableItem and adds no primary
     * virtual methods. Its factory allocates exactly 0xE0 bytes.
     */
    class C_MissileWeaponItem : public C_PickableItem {
      public:
        ~C_MissileWeaponItem() override = default;

        MissileWeaponItemStateLayout *GetMissileWeaponState() { return &m_state; }
        const MissileWeaponItemStateLayout *GetMissileWeaponState() const { return &m_state; }

      private:
        MissileWeaponItemStateLayout m_state; // 0xD8
    };

    static_assert(sizeof(C_MissileWeaponItem) == 0xE0, "C_MissileWeaponItem size mismatch");
} // namespace SDK::wh::entitymodule
