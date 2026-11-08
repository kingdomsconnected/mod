#pragma once

#include "i_geom_manager.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryPhysics geometry manager.
     *
     * CGeomManager's table at 0x183A66788 implements all 23 IGeomManager
     * entries and appends one concrete operation. Its scalar deleting
     * destructor proves the complete 0x58-byte size.
     */
    class CGeomManager : public IGeomManager {
      public:
        ~CGeomManager() override = default;

        virtual void unk_fn0023() = 0; // +0xB8

      private:
        std::byte m_opaque0008[0x50];
    };

    static_assert(sizeof(CGeomManager) == 0x58,
                  "CGeomManager size mismatch");
} // namespace SDK
