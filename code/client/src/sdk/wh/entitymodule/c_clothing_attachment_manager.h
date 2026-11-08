#pragma once

#include "i_clothing_attachment_manager.h"

#include "i_clothing_attachment_owner.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Concrete per-character clothing attachment manager.
     *
     * Its table at 0x183A4EA98 contains all 38 interface entries followed by
     * six opaque concrete operations. The scalar deleting destructor proves
     * the complete 0xD0-byte size.
     */
    class C_ClothingAttachmentManager : public I_ClothingAttachmentManager {
      public:
        ~C_ClothingAttachmentManager() override = default;

        virtual void unk_fn0038() = 0; // first concrete slot, +0x130
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0; // final slot, +0x158

      private:
        // Ctor 0x1807540C4.
        I_ClothingAttachmentOwner *m_pOwner; // 0x08, the ctor's argument
        void *m_value0010;                   // 0x10, the owner's slot 0 result
        void *m_value0018;                   // 0x18, 0x1807546B0 of the owner's slot 0 result
        std::uint64_t m_value0020;           // 0x20, zeroed
        std::uint64_t m_value0028;           // 0x28, zeroed
        std::uint64_t m_value0030;           // 0x30, zeroed
        std::uint32_t m_value0038;           // 0x38, zeroed
        std::byte m_padding003C[0x4];
        std::uint64_t m_value0040;           // 0x40, zeroed
        std::uint64_t m_value0048;           // 0x48, zeroed
        std::uint64_t m_value0050;           // 0x50, zeroed
        std::uint64_t m_value0058;           // 0x58, zeroed
        std::uint64_t m_value0060;           // 0x60, zeroed
        std::uint64_t m_value0068;           // 0x68, zeroed
        std::uint64_t m_value0070;           // 0x70, copied from the global qword_1854AAEF0
        std::byte m_opaque0078[0x8];         // 0x78, head that 0x88 points back at; not written
        std::uint64_t m_value0080;           // 0x80, zeroed
        void *m_pListHead0088;               // 0x88, ctor sets this+0x78
        std::uint64_t m_value0090;           // 0x90, zeroed
        std::uint64_t m_value0098;           // 0x98, zeroed
        std::uint64_t m_value00A0;           // 0xA0, zeroed
        std::uint64_t m_value00A8;           // 0xA8, zeroed
        std::uint32_t m_value00B0;           // 0xB0, 0x180753F0C of the owner's slot 0 result
        std::byte m_value00B4[0x8];          // 0xB4, unaligned qword the ctor zeroes
        std::uint16_t m_value00BC;           // 0xBC, zeroed
        bool m_flag00BE;                     // 0xBE, zeroed
        bool m_isCinematic;                  // 0xBF, the owner's slot 9 name starts with "cin_"
        std::uint16_t m_value00C0;           // 0xC0, ctor sets 1
        std::byte m_padding00C2[0x2];
        std::uint32_t m_value00C4;           // 0xC4, zeroed
        bool m_flag00C8;                     // 0xC8, zeroed
        std::byte m_padding00C9[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_ClothingAttachmentManager, m_pOwner) == 0x08, "C_ClothingAttachmentManager::m_pOwner offset mismatch");
            static_assert(offsetof(C_ClothingAttachmentManager, m_pListHead0088) == 0x88, "C_ClothingAttachmentManager::m_pListHead0088 offset mismatch");
            static_assert(offsetof(C_ClothingAttachmentManager, m_value00B0) == 0xB0, "C_ClothingAttachmentManager::m_value00B0 offset mismatch");
            static_assert(offsetof(C_ClothingAttachmentManager, m_isCinematic) == 0xBF, "C_ClothingAttachmentManager::m_isCinematic offset mismatch");
            static_assert(offsetof(C_ClothingAttachmentManager, m_flag00C8) == 0xC8, "C_ClothingAttachmentManager::m_flag00C8 offset mismatch");
        }
    };

    static_assert(sizeof(C_ClothingAttachmentManager) == 0xD0, "C_ClothingAttachmentManager size mismatch");
} // namespace SDK::wh::entitymodule
