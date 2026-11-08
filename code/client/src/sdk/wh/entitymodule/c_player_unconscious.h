#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player unconscious-state adapter.
     *
     * Its standalone RTTI table at 0x183A80EF0 contains exactly two
     * operations and no virtual destructor. C_Player's construction path
     * stores the owning player at +0x08 and constructs the remaining state at
     * +0x10; explicit teardown followed by a 0x30-byte release proves size.
     */
    class C_PlayerUnconscious {
      public:
        virtual void unk_fn0000() = 0; // emitted no-op
        virtual void unk_fn0001() = 0;

      private:
        C_Player *m_pPlayer; // 0x08
        // 0x10..0x30: an embedded wh::guimodule::C_BasicFader<C_FaderController>
        // built by 0x1808DD5C0 with the buff name "UnconsciousSoulBuffInstance".
        std::byte m_faderVftable0010[0x8];  // 0x10, C_BasicFader<C_FaderController> vftable
        void *m_pFaderOwner0018;            // 0x18, *(gameModules+0xE8)+0x58 passed by factory 0x180BC7118
        SDK::CryStringT<char> m_buffName;   // 0x20, "UnconsciousSoulBuffInstance"
        std::uint64_t m_value0028;          // 0x28, zeroed by 0x1808DD5C0

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerUnconscious, m_pFaderOwner0018) == 0x18, "C_PlayerUnconscious::m_pFaderOwner0018 offset mismatch");
            static_assert(offsetof(C_PlayerUnconscious, m_buffName) == 0x20, "C_PlayerUnconscious::m_buffName offset mismatch");
            static_assert(offsetof(C_PlayerUnconscious, m_value0028) == 0x28, "C_PlayerUnconscious::m_value0028 offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerUnconscious) == 0x30,
                  "C_PlayerUnconscious size mismatch");
} // namespace SDK::wh::entitymodule
