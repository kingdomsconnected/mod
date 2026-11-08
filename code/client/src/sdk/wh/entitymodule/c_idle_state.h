#pragma once

#include "../../types.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * An actor's idle state, embedded at `C_Actor+0x690`.
     *
     * Constructed in place by 0x1808E25C4, which installs both of its
     * vtables, and wired to the actor model by `ConnectModelSignals`
     * (0x1808DDEA8) at the end of the actor constructor. It runs up to the
     * AI animation state at `C_Actor+0x738`.
     */
    class C_IdleState {
      public:
        virtual ~C_IdleState() = default;

      private:
        // Ctor 0x1808E25C4, dtor 0x1808E1C30.
        std::byte m_secondaryVftable0008[0x8];  // 0x08, the second C_IdleState vftable
        SDK::CryStringT<char> m_name0010;       // 0x10, ctor assigns byte_183A3D1E0
        std::uint64_t m_values0018[2];          // 0x18, ctor zeroes
        std::uint32_t m_value0028;              // 0x28, ctor zeroes
        std::byte m_padding002C[0x4];
        void *m_pOwner0030;                     // 0x30, the ctor's argument
        std::byte m_state0038[0x28];            // 0x38, built by 0x1808E2698; carries a CryStringT at +0x40 the dtor releases
        void *m_value0060;                      // 0x60, ctor zeroes; dtor releases it through 0x1804B2F14
        bool m_state0068;                       // 0x68, ctor clears
        std::byte m_padding0069[0x3];
        std::uint32_t m_values006C[2];          // 0x6C, ctor zeroes (one unaligned qword store)
        SDK::wh::shared::C_Signal m_signal0078; // 0x78, dispatch 0x1855D8AC8
        SDK::wh::shared::C_Signal m_signal0088; // 0x88, dispatch 0x1855D8AB8
        SDK::wh::shared::C_Signal m_signal0098; // 0x98, dispatch 0x1855D8AA8

        static void AssertLayout() {
            static_assert(offsetof(C_IdleState, m_name0010) == 0x10, "C_IdleState::m_name0010 offset mismatch");
            static_assert(offsetof(C_IdleState, m_pOwner0030) == 0x30, "C_IdleState::m_pOwner0030 offset mismatch");
            static_assert(offsetof(C_IdleState, m_value0060) == 0x60, "C_IdleState::m_value0060 offset mismatch");
            static_assert(offsetof(C_IdleState, m_values006C) == 0x6C, "C_IdleState::m_values006C offset mismatch");
            static_assert(offsetof(C_IdleState, m_signal0078) == 0x78, "C_IdleState::m_signal0078 offset mismatch");
            static_assert(offsetof(C_IdleState, m_signal0098) == 0x98, "C_IdleState::m_signal0098 offset mismatch");
        }
    };

    static_assert(sizeof(C_IdleState) == 0xA8, "C_IdleState size mismatch");
} // namespace SDK::wh::entitymodule
