#pragma once

#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Why a fast travel may not begin, as C_FastTravel::ValidateTravelStart
     * answers it. Only Ok lets one start; the rest are what the HUD spells
     * out, and the names are the game's own, from its enum name table at
     * 0x182DE2FC8. CantTravel is the general refusal, and what the game
     * itself gives when the player's fast-travel permission bit is clear.
     */
    enum class E_TravelStartResult : std::uint8_t {
        Ok                 = 0,
        CantTravel         = 1,
        CantTravelFromHere = 2,
        Combat             = 3,
        Hungry             = 4,
        Poison             = 5,
        Bleeding           = 6,
        PausedTime         = 7,
        Trespassing        = 8,
        Fatigue            = 9,
        Encumbered         = 10,
        TenseCircumstance  = 11,
    };
} // namespace SDK::wh::playermodule
