#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The opponent seat's dice AI: 0x4A8 bytes, constructor 0x182E80148,
     * installed on seat 1 by `C_DiceGameModel::ResetGame` (0x182E9398C) and
     * taken off seat 0 there. It is a dice-model object only -- it never
     * reads the opponent's brain or XGenAI registration.
     *
     * The game model asks it about each state in `TryAdvanceGameState`
     * (0x182E98DC4), and its presence is also what makes the seat the
     * opponent: `C_Dice::GetThrowActor` keys on it, and the deferred advance
     * that drives the opponent is only scheduled for a seat that has one.
     */
    class C_SimpleDiceAI {
      private:
        // Constructor 0x182E80148 (target score). A "small vector" below is
        // 0x68 bytes: seven 8-byte inline slots, an in-use byte at +0x38, an
        // arena cursor and arena pointer both aimed at the inline slots, then
        // begin/end/capacity; sub_182E79518 reserves seven.
        const void *m_pVTable;                  // 0x000
        void *m_ptrs0008[2];                    // 0x008; ctor 0
        float m_value0018;                      // 0x018; ctor 1.0f
        std::uint32_t m_value001C;              // 0x01C; ctor 0
        std::byte m_opaque0020[0x2];            // 0x020; not written by the constructor
        std::uint16_t m_value0022;              // 0x022; ctor 0
        std::uint8_t m_flag0024;                // 0x024; ctor 0
        std::byte m_opaque0025[0xB];            // 0x025; not written by the constructor
        void *m_vector0030[3];                  // 0x030; begin/end/capacity, ctor 0
        std::byte m_object0048[0x18];           // 0x048; sub_182E7F3E8
        std::byte m_table0060[0x68];            // 0x060; sub_182E7EB54
        std::byte m_smallVector00C8[0x68];      // 0x0C8
        std::byte m_table0130[0x68];            // 0x130; sub_182E7EB54
        std::byte m_smallVectors0198[6][0x68];  // 0x198
        std::byte m_object0408[0x18];           // 0x408; sub_182E7F3E8
        std::byte m_object0420[0x18];           // 0x420; sub_182E7F3E8
        void *m_ptrs0438[6];                    // 0x438; ctor 0
        std::byte m_hashMap0468[0x40];          // 0x468; std::unordered_map (sub_180BBB344)

        static void AssertLayout() {
            static_assert(offsetof(C_SimpleDiceAI, m_value0018) == 0x18, "C_SimpleDiceAI::m_value0018 offset mismatch");
            static_assert(offsetof(C_SimpleDiceAI, m_flag0024) == 0x24, "C_SimpleDiceAI::m_flag0024 offset mismatch");
            static_assert(offsetof(C_SimpleDiceAI, m_table0060) == 0x60, "C_SimpleDiceAI::m_table0060 offset mismatch");
            static_assert(offsetof(C_SimpleDiceAI, m_smallVectors0198) == 0x198, "C_SimpleDiceAI::m_smallVectors0198 offset mismatch");
            static_assert(offsetof(C_SimpleDiceAI, m_ptrs0438) == 0x438, "C_SimpleDiceAI::m_ptrs0438 offset mismatch");
            static_assert(offsetof(C_SimpleDiceAI, m_hashMap0468) == 0x468, "C_SimpleDiceAI::m_hashMap0468 offset mismatch");
        }
    };

    static_assert(sizeof(C_SimpleDiceAI) == 0x4A8, "C_SimpleDiceAI size mismatch");
} // namespace SDK::wh::playermodule
