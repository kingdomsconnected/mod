#pragma once

#include <cstdint>

namespace SDK::wh::shared {
    /**
     * Reflected with RTTR at 0x18005DD90; `ToString` 0x18079B714, `FromString` 0x182BA9064.
     *
     * Store-independent: the Steam app ids these resolve to are compiled in
     * at 0x180387D20 rather than being a column of the table. The three
     * names carrying a comment are the ones the store spells differently.
     */
    struct E_DLC {
        enum Type : std::uint8_t {
            Invalid             = 0,
            QuestForValor       = 1, // "The Lion's Crest"
            Barber              = 2,
            HorseRacing         = 3,
            HardcoreMode        = 4,
            ForgeTycoon         = 5, // "Legacy of the Forge"
            TouristMode         = 6,
            MysteriaEcclesiae   = 7,
            Unpublished         = 8,
            BanditCamps         = 9, // "Brushes with Death"
            SeasonPassShields   = 10,
            GoldEditionHuntsman = 11,
            Last                = 12,
        };
    };
} // namespace SDK::wh::shared
