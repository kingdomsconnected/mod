#pragma once

#include "../../types.h"
#include "c_dice_game_player_model.h"
#include "c_die_object_controller.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** The seat's dice presentation, built by `MakeDieSet` (0x182E7C984). */
    class C_DieSet {
      public:
        const Vec3 &GetCenter() const {
            return m_center;
        }

        C_DiceGamePlayerModel *GetPlayer() const {
            return m_player;
        }

        std::size_t GetDieCount() const {
            return static_cast<std::size_t>(m_diceEnd - m_diceBegin);
        }

        C_DieObjectController *GetDie(std::size_t index) const {
            return index < GetDieCount() ? m_diceBegin[index].get() : nullptr;
        }

      private:
        std::byte m_vftable0000[0x8];      // 0x00; C_DieSet vftable (ctor 0x182E7F924).
        void *m_weakThisPtr;               // 0x08; enable_shared_from_this weak pointer, ctor 0.
        void *m_weakThisRep;               // 0x10; its control block, ctor 0.
        // 0x18: StackVector of six 8-byte elements (inline 0x18, flag 0x48, arena 0x50, vector 0x58..0x78), reserved by sub_182E79664; element type unproven.
        std::byte m_stackVector0018[0x60];
        // 0x78: StackVector of six 16-byte elements (inline 0x78, flag 0xD8, arena 0xE0, vector 0xE8..0x108), one per player die via sub_182E7C8B0; element type unproven.
        std::byte m_stackVector0078[0x78];
        // The vector's live range, traversed by ShowAllDice (0x182E8BDFC).
        // Read in place; never own its buffer.
        std::shared_ptr<C_DieObjectController> *m_diceBegin; // 0xF0
        std::shared_ptr<C_DieObjectController> *m_diceEnd;   // 0xF8
        std::byte m_opaque0100[0x8];
        void *m_pItemVectorHolder0108;     // 0x108; a fresh wh::entitymodule::C_ItemVectorHolder (0x20 bytes).
        void *m_pItemVectorHolder0110;     // 0x110; a second C_ItemVectorHolder.
        Vec3 m_center; // 0x118, `UpdateDieNavigationCameraTransform`'s look target
        std::uint32_t m_value0124;         // 0x124; ctor 0.
        float m_value0128;                 // 0x128; ctor 0.00954f.
        float m_value012C;                 // 0x12C; ctor 0.01908f.
        std::byte m_opaque0130[0x8];
        C_DiceGamePlayerModel *m_player; // 0x138

        static void AssertLayout() {
            static_assert(offsetof(C_DieSet, m_diceBegin) == 0xF0, "C_DieSet::m_diceBegin offset mismatch");
            static_assert(offsetof(C_DieSet, m_diceEnd) == 0xF8, "C_DieSet::m_diceEnd offset mismatch");
            static_assert(offsetof(C_DieSet, m_center) == 0x118, "C_DieSet::m_center offset mismatch");
            static_assert(offsetof(C_DieSet, m_player) == 0x138, "C_DieSet::m_player offset mismatch");
            static_assert(offsetof(C_DieSet, m_stackVector0078) == 0x78, "C_DieSet::m_stackVector0078 offset mismatch");
            static_assert(offsetof(C_DieSet, m_pItemVectorHolder0108) == 0x108, "C_DieSet::m_pItemVectorHolder0108 offset mismatch");
            static_assert(offsetof(C_DieSet, m_value0128) == 0x128, "C_DieSet::m_value0128 offset mismatch");
        }
    };

    static_assert(sizeof(C_DieSet) == 0x140, "C_DieSet size mismatch");
} // namespace SDK::wh::playermodule
