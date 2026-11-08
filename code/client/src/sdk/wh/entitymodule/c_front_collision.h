#pragma once

#include "../../cryengine/physics/i_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player forward-collision adapter.
     *
     * Its standalone RTTI table at 0x183A80FB8 contains exactly six
     * operations and no virtual destructor. Construction stores C_Player at
     * +0x08; the non-virtual destructor releases the physics handle at +0x18.
     * Allocation and sized release prove the exact 0x28-byte layout.
     */
    class C_FrontCollision {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;

      private:
        C_Player *m_pPlayer; // 0x08
        std::int32_t m_value0010;               // 0x10, factory 0x180BC747C sets 1
        std::byte m_padding0014[0x4];
        SDK::IPhysicalEntity *m_pPhysicalEntity; // 0x18, destructor 0x180B52284 destroys it through the physical world (slot 19)
        std::int32_t m_value0020;               // 0x20, factory sets -1
        std::int32_t m_value0024;               // 0x24, factory zeroes it

        static void AssertLayout() {
            static_assert(offsetof(C_FrontCollision, m_value0010) == 0x10, "C_FrontCollision::m_value0010 offset mismatch");
            static_assert(offsetof(C_FrontCollision, m_pPhysicalEntity) == 0x18, "C_FrontCollision::m_pPhysicalEntity offset mismatch");
            static_assert(offsetof(C_FrontCollision, m_value0024) == 0x24, "C_FrontCollision::m_value0024 offset mismatch");
        }
    };

    static_assert(sizeof(C_FrontCollision) == 0x28,
                  "C_FrontCollision size mismatch");
} // namespace SDK::wh::entitymodule
