#pragma once

#include "../../cryengine/action/i_item.h"

#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Concrete base for pickable inventory items.
     *
     * C_PickableItem's primary table at 0x183EC85E0 contains exactly the 130
     * entries represented by IItem and adds no primary virtual slots. Its
     * direct factory allocates exactly 0xD8 bytes; the secondary
     * IGameObjectProfileManager vptr resides at +0x40.
     */
    class C_PickableItem: public SDK::IItem {
      public:
        ~C_PickableItem() override = default;

      private:
        // Ctor 0x1803F08EC.
        std::byte m_profileManagerVftable0040[0x8]; // 0x40, IGameObjectProfileManager vptr
        SDK::wh::shared::C_Signal m_signal0048;     // 0x48, dispatch 0x185667118
        std::uint64_t m_value0058;                  // 0x58, ctor zeroes
        std::uint64_t m_value0060;                  // 0x60, ctor sets 16
        std::uint64_t m_value0068;                  // 0x68, ctor zeroes
        std::uint64_t m_value0070;                  // 0x70, ctor sets 2
        std::uint64_t m_value0078;                  // 0x78, ctor sets 1
        std::byte m_opaque0080[0x38];               // 0x80, ctor leaves it
        std::uint64_t m_value00B8;                  // 0xB8, ctor zeroes
        std::uint32_t m_value00C0;                  // 0xC0, ctor zeroes
        std::byte m_padding00C4[0x4];
        const void *m_pShared00C8;                  // 0xC8, ctor points it at a lazily built static (Str)
        std::uint64_t m_value00D0;                  // 0xD0, ctor zeroes

        static void AssertLayout() {
            static_assert(offsetof(C_PickableItem, m_signal0048) == 0x48, "C_PickableItem::m_signal0048 offset mismatch");
            static_assert(offsetof(C_PickableItem, m_value0078) == 0x78, "C_PickableItem::m_value0078 offset mismatch");
            static_assert(offsetof(C_PickableItem, m_value00B8) == 0xB8, "C_PickableItem::m_value00B8 offset mismatch");
            static_assert(offsetof(C_PickableItem, m_pShared00C8) == 0xC8, "C_PickableItem::m_pShared00C8 offset mismatch");
        }
    };

    static_assert(sizeof(C_PickableItem) == 0xD8, "C_PickableItem size mismatch");
} // namespace SDK::wh::entitymodule
