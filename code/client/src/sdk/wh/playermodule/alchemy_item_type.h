#pragma once

#include <cstdint>

namespace SDK::wh::playermodule {
    /** Native RTTR registration 0x181631088; distinct from usable action IDs. */
    enum class AlchemyItemType : std::int32_t {
        Bowl               = 0,
        Sandglass          = 1,
        Mortar             = 2,
        Pestle             = 3,
        Retort             = 4,
        PotionFlask        = 5,
        Pot                = 6,
        Herb1              = 7,
        Herb2              = 8,
        Herb3              = 9,
        SpecialIngredient1 = 10,
        SpecialIngredient2 = 11,
        SpecialIngredient3 = 12,
        Glass1             = 13,
        Glass2             = 14,
        Glass3             = 15,
        Glass4             = 16,
        FakeFlask          = 17,
        Bellows            = 18,
        Book               = 19,
        PotHalter          = 20,
        None               = 21
    };

    static_assert(sizeof(AlchemyItemType) == 4);
} // namespace SDK::wh::playermodule
