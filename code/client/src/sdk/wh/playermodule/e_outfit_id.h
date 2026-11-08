#pragma once

#include <cstdint>

namespace SDK::wh::playermodule {
    /** Player outfit slot identifier recovered from the game's RTTR metadata. */
    struct E_OutfitId {
        enum Type : std::uint8_t {
            A = 0,
            B = 1,
            C = 2,
        };
    };

    static_assert(sizeof(E_OutfitId::Type) == 0x1,
                  "E_OutfitId::Type size mismatch");
} // namespace SDK::wh::playermodule
