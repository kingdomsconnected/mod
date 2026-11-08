#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Reference-counted local-player tense-circumstance state.
     *
     * Its standalone RTTI table at 0x183A81008 contains exactly three
     * operations and no virtual destructor. C_Player's allocation path stores
     * the owning player at +0x08; the remaining counters and owned runtime
     * handle occupy +0x10 through +0x1F. Sized release proves size 0x20.
     */
    class C_PlayerTenseCircumstance {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;

      private:
        C_Player *m_pPlayer; // 0x08
        std::uint64_t m_value0010; // 0x10, factory 0x180BC778C zeroes
        std::uint64_t m_value0018; // 0x18, factory zeroes

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerTenseCircumstance, m_value0010) == 0x10, "C_PlayerTenseCircumstance::m_value0010 offset mismatch");
            static_assert(offsetof(C_PlayerTenseCircumstance, m_value0018) == 0x18, "C_PlayerTenseCircumstance::m_value0018 offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerTenseCircumstance) == 0x20,
                  "C_PlayerTenseCircumstance size mismatch");
} // namespace SDK::wh::entitymodule
