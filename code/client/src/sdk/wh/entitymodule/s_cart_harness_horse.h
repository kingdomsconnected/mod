#pragma once

#include "../../types.h"
#include "c_actor.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The horse half of one of a cart's harness entries: the 0x48 bytes from
     * +0x10 of the 88-byte entry `BindHarnessEntry` (0x1828DA5EC) fills, which
     * is the pointer `ApplyReinsModifier` (0x1828FA228) is handed every tick
     * the cart's movement walks the animal to its harness helper.
     */
    struct S_CartHarnessHorse {
        C_Actor *GetHorse() const {
            return m_pHorse;
        }

      private:
        C_Actor *m_pHorse;                 // 0x00, the hitched horse
        bool m_rightSlot;                  // 0x08
        std::byte m_padding0009[0x7];
        std::uint64_t m_value0010;         // 0x10, BindHarnessEntry 0x1828DA5EC stores its 4th argument
        CryGUID m_presetId;                // 0x18, EquipHarnessPreset 0x1828F9884: 0x18557AD20/30/40 by slot
        CryStringT<char> m_presetName;     // 0x28, "HorseInCart"
        bool m_flag0030;                   // 0x30, EquipHarnessPreset clears it
        std::byte m_padding0031[0x7];
        void *m_pReinsModifier;            // 0x38, std::shared_ptr of the C_CartHorseReinsModifier pushed onto the horse's skeleton
        void *m_pReinsModifierRef;         // 0x40

        static void AssertLayout() {
            static_assert(offsetof(S_CartHarnessHorse, m_rightSlot) == 0x08, "S_CartHarnessHorse::m_rightSlot offset mismatch");
            static_assert(offsetof(S_CartHarnessHorse, m_presetId) == 0x18, "S_CartHarnessHorse::m_presetId offset mismatch");
            static_assert(offsetof(S_CartHarnessHorse, m_presetName) == 0x28, "S_CartHarnessHorse::m_presetName offset mismatch");
            static_assert(offsetof(S_CartHarnessHorse, m_flag0030) == 0x30, "S_CartHarnessHorse::m_flag0030 offset mismatch");
            static_assert(offsetof(S_CartHarnessHorse, m_pReinsModifier) == 0x38, "S_CartHarnessHorse::m_pReinsModifier offset mismatch");
        }
    };

    static_assert(sizeof(S_CartHarnessHorse) == 0x48, "S_CartHarnessHorse size mismatch");
} // namespace SDK::wh::entitymodule
