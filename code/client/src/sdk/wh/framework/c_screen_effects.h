#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    /**
     * Screen-effect blender owned by C_CameraManager at +0x478.
     *
     * RTTI names it wh::framework::CScreenEffects. Its constructor at
     * 0x180897038 writes the vptr, two std::list heads (each allocating a
     * self-linked sentinel), an int, a word set to 1 and two empty CryStrings.
     * The manager's 0x4D0 size closes it at 0x58 bytes.
     */
    class CScreenEffects {
      public:
        virtual ~CScreenEffects() = default;

      private:
        void *m_pListHead0008;        // 0x08, std::list sentinel (0x30-byte nodes)
        std::uint64_t m_listSize0010; // 0x10
        void *m_pListHead0018;        // 0x18, std::list sentinel (0x28-byte nodes)
        std::uint64_t m_listSize0020; // 0x20
        std::int32_t m_state0028;     // 0x28, ctor sets 0
        std::uint16_t m_state002C;    // 0x2C, ctor sets 1
        std::byte m_unset002E[0x2];   // 0x2E, not written by the ctor
        const char *m_string0030;     // 0x30, CryStringT<char> payload, ctor sets empty
        const char *m_string0038;     // 0x38, CryStringT<char> payload, ctor sets empty
        std::byte m_unset0040[0x18];  // 0x40, not written by the ctor

        static void AssertLayout() {
            static_assert(offsetof(CScreenEffects, m_pListHead0008) == 0x08, "CScreenEffects::m_pListHead0008 offset mismatch");
            static_assert(offsetof(CScreenEffects, m_state0028) == 0x28, "CScreenEffects::m_state0028 offset mismatch");
            static_assert(offsetof(CScreenEffects, m_string0030) == 0x30, "CScreenEffects::m_string0030 offset mismatch");
        }
    };

    static_assert(sizeof(CScreenEffects) == 0x58, "CScreenEffects size mismatch");
} // namespace SDK::wh::framework
