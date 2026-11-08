#pragma once

#include "i_component_event_distributer.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete entity-component event distributor owned by CEntitySystem.
     *
     * Its table at 0x183A87AA0 contains exactly the two
     * IComponentEventDistributer entries and adds no primary virtual
     * operations. Owner construction and scalar deletion prove size 0x98.
     */
    class CComponentEventDistributer : public IComponentEventDistributer {
      public:
        ~CComponentEventDistributer() override = default;

      private:
        std::byte m_opaque0008[0x90];
    };

    static_assert(sizeof(CComponentEventDistributer) == 0x98,
                  "CComponentEventDistributer size mismatch");
} // namespace SDK
