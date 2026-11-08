#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class I_Soul;
    // Stack construction at 0x180726808; modifier gate at 0x180469FF8.
    // The additional references remain opaque; only action-local identity
    // comparisons are supported until their concrete types are established.
    struct S_SoulQueryContext {
        const I_Soul *otherSoul;
        const void *reference08;
        const void *reference10;
        std::uint32_t reason;
        std::byte m_padding001C[4];
        const void *reference20;

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_SoulQueryContext, otherSoul) == 0);
            static_assert(offsetof(S_SoulQueryContext, reference08) == 8);
            static_assert(offsetof(S_SoulQueryContext, reference10) == 0x10);
            static_assert(offsetof(S_SoulQueryContext, reason) == 0x18);
            static_assert(offsetof(S_SoulQueryContext, reference20) == 0x20);
        }
    };
    static_assert(sizeof(S_SoulQueryContext) == 0x28);
} // namespace SDK::wh::rpgmodule
