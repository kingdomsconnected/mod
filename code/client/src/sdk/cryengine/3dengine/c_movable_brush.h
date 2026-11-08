#pragma once

#include "c_owned_brush.h"

#include <cstddef>

namespace SDK {
    /**
     * Entity-owned movable static-geometry render node.
     *
     * RTTI proves the COwnedBrush base at +0x00. Construction only replaces
     * the primary table, sized allocation remains 0x108 bytes, and all 78
     * entries are inherited except GetRenderNodeType() at slot 7
     * (0x181A7DFD0 in this class's table at 0x184020370), which returns 17 --
     * `eERType_MovableBrush` in the numbering the engine's own name table
     * declares, not the terrain sector that later CryEngine numbers 17.
     */
    class CMovableBrush : public COwnedBrush {
      public:
        static constexpr EERType FactoryType = eERType_MovableBrush;

        ~CMovableBrush() override = default;
    };

    static_assert(sizeof(CMovableBrush) == 0x108,
                  "CMovableBrush size mismatch");
} // namespace SDK
