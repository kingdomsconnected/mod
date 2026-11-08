#pragma once

#include "c_projectile.h"

namespace SDK::wh::entitymodule {
    // RTTI: C_Decoy -> CProjectile. Factory 0x1829440D0 allocates 0x168;
    // the collision override owns two bytes after the projectile base.
    class C_Decoy: public CProjectile {
      public:
        static bool Is(const CProjectile &projectile) {
            return *reinterpret_cast<const std::uintptr_t *>(&projectile) == gPatterns.C_Decoy__VTable;
        }

      private:
        std::uint16_t m_collisionState0160; // 0x160, factory 0x1829440D0 zeroes it
        std::byte m_padding0162[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_Decoy, m_collisionState0160) == 0x160, "C_Decoy::m_collisionState0160 offset mismatch");
        }
    };

    static_assert(sizeof(C_Decoy) == 0x168, "C_Decoy size mismatch");
} // namespace SDK::wh::entitymodule
