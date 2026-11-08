#pragma once

#include <cstdint>

namespace SDK {
    struct pe_params {
        std::int32_t type;
    };

    static_assert(sizeof(pe_params) == 0x04, "pe_params size mismatch");
} // namespace SDK
