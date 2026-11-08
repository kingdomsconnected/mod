#pragma once

#include "c_physical_entity.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryPhysics particle entity.
     *
     * RTTI proves the global hierarchy CParticleEntity -> CPhysicalEntity.
     * Its table at 0x183B0C698 has exactly the 75 inherited entries and adds
     * no primary virtual operations. Factory allocation and vector deletion
     * independently prove the complete 0x240-byte size.
     */
    class CParticleEntity : public CPhysicalEntity {
      public:
        ~CParticleEntity() override = default;

      private:
        std::byte m_opaque0158[0xE8];
    };

    static_assert(sizeof(CParticleEntity) == 0x240,
                  "CParticleEntity size mismatch");
} // namespace SDK
