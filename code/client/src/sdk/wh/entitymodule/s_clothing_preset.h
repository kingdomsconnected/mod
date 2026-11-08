#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One row of `Libs/Tables/item/clothing_preset.xml`.
     *
     * RTTR registers it as `wh::entitymodule::clothing_preset`
     * (`RegisterClothingPresetReflection_180D63C0C`) with the table's own
     * column names: `clothing_preset_id`, `clothing_preset_name`, `gender`,
     * `prefers_hood_on`, `social_class_id`, `wealth_level` and `Items`. Rows
     * are stored by value in the database's vector with a 0x60 stride
     * (`FinalizeLoad_180F65004`).
     */
    struct S_ClothingPreset {
        // 0x00..0x1C: the C_ItemHealthProvider base; ctor 0x180D8C748
        // installs it and then the S_ClothingPreset vftable.
        std::byte m_vftable0000[0x8]; // 0x00, S_ClothingPreset vftable
        std::uint32_t m_value0008;    // 0x08, ctor zeroes
        float m_value000C;            // 0x0C, ctor sets -1.0
        std::uint32_t m_value0010;    // 0x10, ctor zeroes
        float m_value0014;            // 0x14, ctor sets -1.0
        std::uint32_t m_value0018;    // 0x18, ctor zeroes
        std::byte m_padding001C[0x4];

        /** `clothing_preset_id`, the key `FindByGuid_18087E31C` compares. */
        CryGUID m_id; // 0x20

        /** `clothing_preset_name`, the row's one CryStringT property. */
        CryStringT<char> m_name;    // 0x30
        bool m_flag0038;            // 0x38, ctor zeroes; copy 0x180D8C830 copies it as a byte
        bool m_flag0039;            // 0x39, ctor zeroes
        std::byte m_padding003A[0x2];
        std::int32_t m_value003C;   // 0x3C, ctor sets 1
        std::int32_t m_value0040;   // 0x40, ctor zeroes
        std::byte m_padding0044[0x4];

        /**
         * `Items`: item classes, one per piece. `ExpandClothingPreset_181EDE6F0`
         * keeps only the armor classes among them.
         */
        StdVector<CryGUID> m_items; // 0x48

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_ClothingPreset, m_id) == 0x20, "S_ClothingPreset::m_id offset mismatch");
            static_assert(offsetof(S_ClothingPreset, m_name) == 0x30, "S_ClothingPreset::m_name offset mismatch");
            static_assert(offsetof(S_ClothingPreset, m_value003C) == 0x3C, "S_ClothingPreset::m_value003C offset mismatch");
            static_assert(offsetof(S_ClothingPreset, m_items) == 0x48, "S_ClothingPreset::m_items offset mismatch");
        }
    };

    static_assert(sizeof(S_ClothingPreset) == 0x60, "S_ClothingPreset size mismatch");
} // namespace SDK::wh::entitymodule
