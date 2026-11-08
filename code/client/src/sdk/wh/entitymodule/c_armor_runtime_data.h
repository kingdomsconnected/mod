#pragma once

#include "../../patterns.h"
#include "c_missile_weapon_runtime_data.h"

#include <utils/hooking/hooking.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What a piece of armour has on it besides itself: its dirt and its blood.
     *
     * Every item of `E_ItemClassId::Armor` gets one, made by
     * `C_ArmorExtraPropertiesCreator::CreateRuntimeData_18046FA8C` from a
     * pool of 0xC0-byte blocks (ctor 0x18046FAB4). The setters below mark
     * what changed and announce it by name -- "armor_dirt", "armor_blood" --
     * through `I_ItemRuntimeData::NotifyPropertyChanged_180470208`, which is
     * what the clothing's visual listeners (0x180974104, 0x180974A14) redraw
     * the piece from.
     */
    class C_ArmorRuntimeData: public C_EquippableItemRuntimeData {
      public:
        /** The body zones blood is kept for, one byte each. */
        static constexpr std::size_t kBloodZoneCount = 6;

        /** The dirt as stored: 0 clean to 255 filthy. */
        std::uint8_t GetDirt() const {
            return m_dirt;
        }

        const std::array<std::uint8_t, kBloodZoneCount> &GetBlood() const {
            return m_blood;
        }

        /**
         * Stores `dirt` (0 to 1) as `int(dirt * 255)`, truncating; nothing
         * happens when that is the byte already there.
         *
         * The native takes the value in EDX, not XMM1: its parameter is a
         * four-byte clamped-float wrapper passed by value, which MSVC puts in
         * an integer register -- every caller clamps and then `movd edx,
         * xmm`. So the bits go as an integer.
         */
        void SetDirt(float dirt) {
            hook::this_call<void>(gPatterns.C_ArmorRuntimeData__SetDirt, this, std::bit_cast<std::uint32_t>(dirt));
        }

        /** Stores all six zones; nothing happens when they already match. */
        void SetBlood(const std::array<std::uint8_t, kBloodZoneCount> &blood) {
            hook::this_call<void>(gPatterns.C_ArmorRuntimeData__SetBlood, this, blood.data());
        }

      private:
        std::uint8_t m_dirt;                               // 0xA8
        std::array<std::uint8_t, kBloodZoneCount> m_blood; // 0xA9
        std::byte m_padding00AF[0x1];
        std::uint32_t m_updateFlags;  // 0xB0, bit 0 dirt (SetDirt), bit 1 scratches (vslot 1, after a health write)
        std::uint32_t m_bloodUpdated; // 0xB4, set to 1 by SetBlood and ClearBlood_1809DCF44
        std::uint32_t m_value00B8;    // 0xB8, the ctor's third argument
        std::byte m_padding00BC[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_ArmorRuntimeData, m_dirt) == 0xA8, "C_ArmorRuntimeData::m_dirt offset mismatch");
            static_assert(offsetof(C_ArmorRuntimeData, m_blood) == 0xA9, "C_ArmorRuntimeData::m_blood offset mismatch");
            static_assert(offsetof(C_ArmorRuntimeData, m_updateFlags) == 0xB0, "C_ArmorRuntimeData::m_updateFlags offset mismatch");
            static_assert(offsetof(C_ArmorRuntimeData, m_bloodUpdated) == 0xB4, "C_ArmorRuntimeData::m_bloodUpdated offset mismatch");
            static_assert(offsetof(C_ArmorRuntimeData, m_value00B8) == 0xB8, "C_ArmorRuntimeData::m_value00B8 offset mismatch");
        }
    };

    static_assert(sizeof(C_ArmorRuntimeData) == 0xC0, "C_ArmorRuntimeData size mismatch");
} // namespace SDK::wh::entitymodule
