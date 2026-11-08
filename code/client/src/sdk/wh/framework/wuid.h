#pragma once

#include <cstdint>

namespace SDK::wh::framework {
    /** Warhorse unique identifier; comparison and storage use one 64-bit value. */
    struct WUID {
        /** What the game's lookups answer for no object at all (`qword_18533A478`, `qword_18533AEE8`). */
        static constexpr std::uint64_t kInvalid = ~0ULL;

        std::uint64_t value;
    };

    static_assert(sizeof(WUID) == 0x8, "WUID size mismatch");
} // namespace SDK::wh::framework
