#pragma once

#include "../entitymodule/c_item.h"
#include "c_dice_game_player_model.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * One player's dice badge: 0x78 bytes, built by `MakeDiceBadge`
     * (0x182E7C514) around constructor 0x182E7F30C. Every player model has
     * one; what makes it a badge is the inventory item bound at +0x18 by
     * `BindBackingItemById` (0x182E94B54). With no item `GetBadgeType`
     * answers 13, InvalidBadge, and every badge path in scoring, rolling
     * and the controls treats the seat as having none.
     */
    class C_DiceBadge {
      public:
        entitymodule::C_Item *GetBackingItem() const {
            return m_pBackingItem;
        }

      private:
        std::byte m_vftable0000[0x8];         // 0x00; C_DiceBadge vftable.
        void *m_weakThisPtr;                  // 0x08; enable_shared_from_this weak pointer, ctor 0.
        void *m_weakThisRep;                  // 0x10; its control block, ctor 0.
        entitymodule::C_Item *m_pBackingItem; // 0x18
        void *m_ptr0020;                      // 0x20; ctor 0.
        C_DiceGamePlayerModel *m_pPlayer;     // 0x28; the constructor's argument.
        std::uint32_t m_value0030;            // 0x30; ctor 0.
        std::byte m_padding0034[0x4];
        // 0x38: an MSVC std::unordered_map (max load factor 1.0f, list, bucket vector, mask 7, maxidx 8)
        // filled by InitializeBadgeRuleTable 0x182E88590; key and value types unproven.
        std::byte m_ruleTable0038[0x40];

        static void AssertLayout() {
            static_assert(offsetof(C_DiceBadge, m_pBackingItem) == 0x18, "C_DiceBadge::m_pBackingItem offset mismatch");
            static_assert(offsetof(C_DiceBadge, m_pPlayer) == 0x28, "C_DiceBadge::m_pPlayer offset mismatch");
            static_assert(offsetof(C_DiceBadge, m_ruleTable0038) == 0x38, "C_DiceBadge::m_ruleTable0038 offset mismatch");
        }
    };

    static_assert(sizeof(C_DiceBadge) == 0x78, "C_DiceBadge size mismatch");
} // namespace SDK::wh::playermodule
