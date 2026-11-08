#pragma once

#include "c_projectile.h"

namespace SDK::wh::entitymodule {
    // Ctor 0x182ACAEF8 (after CProjectile).
    class CArrow: public CProjectile {
      private:
        std::uint32_t m_value0160;                // 0x160, ctor zeroes
        std::byte m_padding0164[0x4];
        SDK::CryStringT<char> m_name0168;         // 0x168, ctor assigns the empty string
        std::byte m_inlineArena0170[0x190];       // 0x170, inline storage for two 0xC8-byte records
        bool m_inlineArenaUsed;                   // 0x300, set by 0x182AC84D4 when the vector takes the inline storage
        std::byte m_padding0301[0x7];
        void *m_pArenaCursor0308;                 // 0x308, ctor points it at m_inlineArena0170
        void *m_pRecordsArena;                    // 0x310, allocator state of the vector below; ctor points it at m_inlineArena0170
        StdVector<std::byte> m_records;           // 0x318, 0xC8-byte records; ctor reserves 2 (0x182AC84D4)
        void *m_value0330;                        // 0x330, ctor zeroes
        std::uint32_t m_value0338;                // 0x338, ctor zeroes
        float m_value033C;                        // 0x33C, ctor sets 1.0
        float m_values0340[3];                    // 0x340, ctor zeroes
        std::uint32_t m_value034C;                // 0x34C, ctor zeroes

        static void AssertLayout() {
            static_assert(offsetof(CArrow, m_name0168) == 0x168, "CArrow::m_name0168 offset mismatch");
            static_assert(offsetof(CArrow, m_inlineArenaUsed) == 0x300, "CArrow::m_inlineArenaUsed offset mismatch");
            static_assert(offsetof(CArrow, m_records) == 0x318, "CArrow::m_records offset mismatch");
            static_assert(offsetof(CArrow, m_value033C) == 0x33C, "CArrow::m_value033C offset mismatch");
        }
    };

    static_assert(sizeof(CArrow) == 0x350, "CArrow size mismatch");
} // namespace SDK::wh::entitymodule
