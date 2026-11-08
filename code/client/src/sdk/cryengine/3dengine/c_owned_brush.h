#pragma once

#include "c_brush.h"

#include <cstddef>

namespace SDK {
    class IEntity;

    /**
     * Owner-associated static-geometry render node.
     *
     * RTTI proves a single CBrush base at +0x00. The 78-entry table has the
     * same extent as CBrush and overrides only two inherited owner-state
     * operations, which write/read the terminal IEntity pointer at +0x100.
     * Default construction clears that pointer and sized deletion proves
     * 0x108 bytes.
     */
    class COwnedBrush : public CBrush {
      public:
        ~COwnedBrush() override = default;

        IEntity *GetOwnerEntityRaw() const { return m_pOwnerEntity; }

      private:
        IEntity *m_pOwnerEntity; // 0x100
    };

    static_assert(sizeof(COwnedBrush) == 0x108,
                  "COwnedBrush size mismatch");
} // namespace SDK
