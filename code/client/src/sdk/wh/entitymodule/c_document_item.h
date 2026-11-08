#pragma once

#include "../../types.h"
#include "../framework/i_ui_book.h"
#include "c_pickable_item.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * A readable document in the world or in hand: entity class `Book`
     * (`Scripts/Entities/Items/XML/Book.xml`, script Book.lua), 0x178 bytes
     * (factory 0x1812765FC, constructor 0x181276680), primary table
     * 0x183B466F0. IItem slot 129 answers 2 for it, which is how the reading
     * minigame tells a document from any other pickable.
     *
     * `OpenBook` binds the shared book screen, renders it onto the page
     * submaterial and, for a GeneralBook document, hands the screen its
     * content through `PushContentToUI` (0x182A6AEBC) -- the single point
     * every readable document's text passes.
     */
    class C_DocumentItem: public C_PickableItem {
      public:
        /** Which movie the script's `GfxName` named: 1 GeneralBook, 2 AlchemyBook, 0 before binding. */
        enum class E_BookKind : std::uint8_t {
            Unbound     = 0,
            GeneralBook = 1,
            AlchemyBook = 2,
        };

        /** The shared screen `BindBookUI` stored; null until the book is opened. */
        framework::I_UIBook *GetBookUI() const {
            return m_pBookUI;
        }

        E_BookKind GetBookKind() const {
            return m_bookKind;
        }

      private:
        framework::I_UIBook *m_pBookUI; // 0xD8
        // Ctor 0x181276680 / dtor 0x1814C4BF8.
        std::uint32_t m_state00E0;              // 0xE0, ctor zeroes
        std::byte m_padding00E4[0x4];           // 0xE4
        SDK::CryStringT<char> m_string00E8;     // 0xE8, ctor sets the empty string
        void *m_value00F0;                      // 0xF0, ctor zeroes
        float m_value00F8;                      // 0xF8, ctor sets 1.0
        float m_value00FC;                      // 0xFC, ctor zeroes
        std::uint16_t m_state0100;              // 0x100, ctor zeroes
        std::uint8_t m_state0102;               // 0x102, ctor zeroes
        std::byte m_padding0103[0x5];           // 0x103
        SDK::CryStringT<char> m_strings0108[4]; // 0x108, ctor sets each to the empty string
        std::byte m_opaque0128[0x4];            // 0x128
        std::uint16_t m_state012C;              // 0x12C, ctor zeroes
        std::byte m_padding012E[0x2];           // 0x12E
        std::uint32_t m_state0130;              // 0x130, ctor zeroes
        std::byte m_value0134[0x10];            // 0x134, ctor copies the 16-byte constant at 0x183A38B00
        std::byte m_opaque0144[0x4];            // 0x144
        std::byte m_vector0148[0x18];           // 0x148, native std::vector (built by 0x1807046CC, freed with _Tidy)
        std::uint64_t m_value0160;              // 0x160, zeroed by 0x1807046CC
        std::uint16_t m_value0168;              // 0x168, zeroed by 0x1807046CC
        std::byte m_opaque016A[0x6];            // 0x16A, not written by the ctor
        E_BookKind m_bookKind; // 0x170
        std::byte m_padding0171[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_DocumentItem, m_pBookUI) == 0xD8, "C_DocumentItem::m_pBookUI offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_bookKind) == 0x170, "C_DocumentItem::m_bookKind offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_string00E8) == 0xE8, "C_DocumentItem::m_string00E8 offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_strings0108) == 0x108, "C_DocumentItem::m_strings0108 offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_vector0148) == 0x148, "C_DocumentItem::m_vector0148 offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_value0160) == 0x160, "C_DocumentItem::m_value0160 offset mismatch");
            static_assert(offsetof(C_DocumentItem, m_value0168) == 0x168, "C_DocumentItem::m_value0168 offset mismatch");
        }
    };

    static_assert(sizeof(C_DocumentItem) == 0x178, "C_DocumentItem size mismatch");
} // namespace SDK::wh::entitymodule
