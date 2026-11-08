#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    struct S_ItemClassGUID {
        std::uint64_t m_low;
        std::uint64_t m_high;
    };

    static_assert(sizeof(S_ItemClassGUID) == 0x10,
                  "S_ItemClassGUID size mismatch");
} // namespace SDK::wh::entitymodule
