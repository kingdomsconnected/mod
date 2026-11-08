#pragma once

#include "c_physical_entity.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryPhysics soft-body entity.
     *
     * RTTI proves the global hierarchy CSoftEntity -> CPhysicalEntity. Its
     * table at 0x184080230 has exactly the 75 inherited entries and adds no
     * primary virtual operations. Factory allocation and vector deletion
     * independently prove the complete 0x2D8-byte size.
     */
    class CSoftEntity : public CPhysicalEntity {
      public:
        ~CSoftEntity() override = default;

      private:
        std::byte m_opaque0158[0x180];
    };

    static_assert(sizeof(CSoftEntity) == 0x2D8,
                  "CSoftEntity size mismatch");
} // namespace SDK
