#pragma once

#include "c_physical_entity.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryPhysics rope entity.
     *
     * RTTI proves the global hierarchy CRopeEntity -> CPhysicalEntity. Its
     * table at 0x183B53170 has exactly the 75 inherited entries and adds no
     * primary virtual operations. Factory allocation and vector deletion
     * independently prove the complete 0x380-byte size.
     */
    class CRopeEntity : public CPhysicalEntity {
      public:
        ~CRopeEntity() override = default;

      private:
        std::byte m_opaque0158[0x228];
    };

    static_assert(sizeof(CRopeEntity) == 0x380,
                  "CRopeEntity size mismatch");
} // namespace SDK
