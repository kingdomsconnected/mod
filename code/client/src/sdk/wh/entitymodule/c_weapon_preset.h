#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One `weapon_preset_item` of a weapon preset.
     *
     * RTTR registers `item_class_id` and `amount`, the latter defaulting to 1
     * (0x180116A70). `ExpandWeaponPreset_181EDEA50` walks them with a 0x20
     * stride and makes a non-stacking class `amount` times.
     */
    struct S_WeaponPresetItem {
        std::byte m_vftable0000[0x8];  // 0x00, S_WeaponPresetItem vftable (copy 0x182946C70)
        CryGUID m_itemClassId;         // 0x08
        std::int32_t m_amount;         // 0x18
        std::byte m_padding001C[0x4];  // the copy at 0x182946C70 stops at 0x1C

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_WeaponPresetItem, m_itemClassId) == 0x08, "S_WeaponPresetItem::m_itemClassId offset mismatch");
            static_assert(offsetof(S_WeaponPresetItem, m_amount) == 0x18, "S_WeaponPresetItem::m_amount offset mismatch");
        }
    };

    static_assert(sizeof(S_WeaponPresetItem) == 0x20, "S_WeaponPresetItem size mismatch");

    /**
     * One row of `Libs/Tables/item/weapon_preset.xml`, stored by value in the
     * database's vector with a 0x58 stride (`FinalizeLoad_180ED0A68`).
     */
    class C_WeaponPreset {
      public:
        const CryGUID &GetId() const {
            return m_id;
        }

      private:
        // Ctor 0x180D8C354.
        std::byte m_vftable0000[0x8];    // 0x00, C_WeaponPreset vftable
        std::uint32_t m_value0008;       // 0x08, ctor zeroes
        float m_value000C;               // 0x0C, ctor sets -1.0
        std::uint32_t m_value0010;       // 0x10, ctor zeroes
        float m_value0014;               // 0x14, ctor sets -1.0
        std::uint32_t m_value0018;       // 0x18, ctor zeroes
        std::byte m_padding001C[0x4];

        /** `weapon_preset_id`, the key `FindByGuid_18087E5E0` compares. */
        CryGUID m_id; // 0x20

        SDK::CryStringT<char> m_name0030; // 0x30, ctor sets the empty string
        std::uint32_t m_value0038;        // 0x38, ctor sets 1
        std::uint32_t m_value003C;        // 0x3C, ctor zeroes

        /** The `weapon_preset_item` rows, read by `ExpandWeaponPreset_181EDEA50`. */
        StdVector<S_WeaponPresetItem> m_items; // 0x40

        static void AssertLayout() {
            static_assert(offsetof(C_WeaponPreset, m_id) == 0x20, "C_WeaponPreset::m_id offset mismatch");
            static_assert(offsetof(C_WeaponPreset, m_items) == 0x40, "C_WeaponPreset::m_items offset mismatch");
            static_assert(offsetof(C_WeaponPreset, m_value000C) == 0x0C, "C_WeaponPreset::m_value000C offset mismatch");
            static_assert(offsetof(C_WeaponPreset, m_name0030) == 0x30, "C_WeaponPreset::m_name0030 offset mismatch");
        }
    };

    static_assert(sizeof(C_WeaponPreset) == 0x58, "C_WeaponPreset size mismatch");
} // namespace SDK::wh::entitymodule
