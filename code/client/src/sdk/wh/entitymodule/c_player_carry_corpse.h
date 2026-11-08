#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player corpse-carry state adapter.
     *
     * C_Player constructs this owned object with itself at +0x08 and a clear
     * state byte at +0x10. Its standalone RTTI table at 0x183A80FF0 contains
     * exactly two operations and no virtual destructor; the following COL
     * proves the boundary. Allocation and sized release prove size 0x18.
     */
    class C_PlayerCarryCorpse {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;

      private:
        C_Player *m_pPlayer;       // 0x08
        bool m_state0010; // 0x10, factory 0x180BC7718 clears it
        std::byte m_padding0011[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerCarryCorpse, m_state0010) == 0x10, "C_PlayerCarryCorpse::m_state0010 offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerCarryCorpse) == 0x18,
                  "C_PlayerCarryCorpse size mismatch");
} // namespace SDK::wh::entitymodule
