#pragma once

#include "i_breakable_manager.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete breakable-prop manager owned by CEntitySystem.
     *
     * Its table at 0x183D28108 contains exactly the 26 IBreakableManager
     * entries and adds no primary virtual operations. The owner allocation
     * and scalar deleting destructor prove the exact 0x48-byte layout.
     */
    class CBreakableManager : public IBreakableManager {
      public:
        ~CBreakableManager() override = default;

      private:
        std::byte m_opaque0008[0x40];
    };

    static_assert(sizeof(CBreakableManager) == 0x48,
                  "CBreakableManager size mismatch");
} // namespace SDK
