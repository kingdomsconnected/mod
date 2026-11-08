#pragma once

#include "../../cryengine/physics/i_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player rigid-body collision adapter.
     *
     * Its standalone RTTI table at 0x183A80F78 contains six collision
     * operations followed by the virtual destructor. The constructor stores
     * C_Player at +0x08, while destruction releases the two physics handles
     * in the opaque tail. Scalar deletion proves the exact 0x30-byte size.
     */
    class C_RigidOnlyCollision {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual ~C_RigidOnlyCollision() = default; // slot 6, +0x30

      private:
        C_Player *m_pPlayer; // 0x08
        std::int32_t m_value0010;                 // 0x10, factory 0x180BC73EC sets 1
        std::byte m_padding0014[0x4];
        SDK::IPhysicalEntity *m_pPhysicalEntity0018; // 0x18, destructor 0x180B525E4 destroys it through the physical world (slot 19)
        SDK::IPhysicalEntity *m_pPhysicalEntity0020; // 0x20, destroyed first by the same destructor
        std::int32_t m_value0028;                 // 0x28, factory sets -1
        std::byte m_padding002C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_RigidOnlyCollision, m_pPhysicalEntity0018) == 0x18, "C_RigidOnlyCollision::m_pPhysicalEntity0018 offset mismatch");
            static_assert(offsetof(C_RigidOnlyCollision, m_pPhysicalEntity0020) == 0x20, "C_RigidOnlyCollision::m_pPhysicalEntity0020 offset mismatch");
            static_assert(offsetof(C_RigidOnlyCollision, m_value0028) == 0x28, "C_RigidOnlyCollision::m_value0028 offset mismatch");
        }
    };

    static_assert(sizeof(C_RigidOnlyCollision) == 0x30,
                  "C_RigidOnlyCollision size mismatch");
} // namespace SDK::wh::entitymodule
