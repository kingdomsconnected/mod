#pragma once

#include "c_item_slot.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Book-specific world-item slot with an exact eight-byte state tail. */
    class C_ItemSlotBook : public C_ItemSlot {
      public:
        float GetState00E0() const { return m_state00E0; }
        float GetState00E4() const { return m_state00E4; }

      private:
        float m_state00E0; // initialized to 0.1f
        float m_state00E4; // initialized to 1.9f
    };

    static_assert(sizeof(C_ItemSlotBook) == 0xE8,
                  "C_ItemSlotBook size mismatch");
} // namespace SDK::wh::entitymodule
