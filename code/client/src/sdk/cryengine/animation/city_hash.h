#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>
#include <cstring>

namespace SDK {
    /**
     * `CityHash64Lowercase` (0x1806A3014): CryAnimation's key for clip paths and names. Animation
     * sets and the global clip table are both keyed by it, so a hash computed any other way
     * finds nothing.
     */
    inline std::uint64_t CityHash64Lowercase(const char *text) {
        return hook::call<std::uint64_t>(gPatterns.CityHash64Lowercase, text, static_cast<unsigned int>(std::strlen(text)));
    }
} // namespace SDK
