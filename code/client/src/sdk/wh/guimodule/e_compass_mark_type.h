#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The mark type at `C_CompassMark + 0x4C`.
     *
     * The names are the game's own, taken from the switch in
     * `wh::guimodule::MarkTypeToIconName` (0x180C4D664) that turns each value
     * into a flash icon name. 33, 35, 40, 41, 54 and 60..68 have no case and
     * render with no icon. `MineEnrtrance` and `Weaponsmiths` are spelled as
     * the binary spells them.
     */
    enum class E_CompassMarkType : std::int32_t {
        Checkpoint      = 0, // the marker the player drops on the map
        Main            = 1,
        Side            = 2,
        Micro           = 3,
        QuestGiver      = 4,
        ActivityGiver   = 5,
        Hub             = 6,
        FastTravel      = 7,
        FastTravelLevel = 8,
        Dog             = 9,
        Dlcs            = 10,
        Home            = 11,
        Shop            = 12,
        Blacksmith      = 13,
        Armourer        = 14,
        Weaponsmiths    = 15,
        Shoemaker       = 16,
        Tailor          = 17,
        Pub             = 18,
        Apothecary      = 19,
        Herbalist       = 20,
        Miller          = 21,
        HorseTrader     = 22,
        Saddler         = 23,
        Bailiff         = 24,
        Arena           = 25,
        ArcheryArena    = 26,
        FistFight       = 27,
        Alchemy         = 28,
        SharpeningWheel = 29,
        Baths           = 30,
        HuntingSpot     = 31,
        HuntingSpotBoar = 32,
        FishingSpot     = 34,
        Hive            = 36,
        ForestGarden    = 37,
        Camp            = 38,
        CampEnemy       = 39,
        MineEnrtrance   = 42,
        Grave           = 43,
        Nest            = 44,
        ConcCross       = 45,
        Shrine          = 46,
        Bed             = 47,
        GeneralPoi      = 48,
        Bakery          = 49,
        Butchery        = 50,
        VegetableShop   = 51,
        Tanner          = 52,
        Hunter          = 53,
        Scribe          = 55,
        Hotel           = 56,
        HuntingSpotRoe  = 57,
        HuntingSpotWolf = 58,
        Smithy          = 59,
        SkillTeacher    = 69,
        FightArena      = 70,
        PoiTipster      = 71,
        SmokingFood     = 72,
        DryingFood      = 73,
        Washing         = 74,
        Indulgences     = 75,
        Gunsmith        = 76,
        DiceTable       = 77,
        FistFightArena  = 78,
        DLC0            = 79,
        BedPlayer       = 80,
        Barber          = 81,
        ShieldPainter   = 82,
        DLC1            = 83,
        DLC2            = 84,
        DLC3            = 85,
        Racing          = 86,
        DLC2_smithing   = 87,
        DLC2_dice       = 88,

        DLC2_acquiringPackages = 89,
        DLC2_archery           = 90,
        DLC2_donations         = 91,
        DLC2_duels             = 92,
        DLC2_stealingPackages  = 93,
        DLC2_activities        = 94,

        SellingChest     = 95,
        FastTravelSedlec = 96,
    };

    struct SCompassMarkTypeName {
        E_CompassMarkType type;
        const char *name;
    };

    /**
     * The icon table, transcribed rather than called: the native's prologue is
     * five bytes and shared with hundreds of unrelated functions, so it has no
     * pattern worth pinning.
     */
    inline constexpr SCompassMarkTypeName kCompassMarkTypeNames[] = {
        {E_CompassMarkType::Checkpoint, "Checkpoint"},
        {E_CompassMarkType::Main, "Main"},
        {E_CompassMarkType::Side, "Side"},
        {E_CompassMarkType::Micro, "Micro"},
        {E_CompassMarkType::QuestGiver, "QuestGiver"},
        {E_CompassMarkType::ActivityGiver, "ActivityGiver"},
        {E_CompassMarkType::Hub, "Hub"},
        {E_CompassMarkType::FastTravel, "FastTravel"},
        {E_CompassMarkType::FastTravelLevel, "FastTravelLevel"},
        {E_CompassMarkType::Dog, "Dog"},
        {E_CompassMarkType::Dlcs, "Dlcs"},
        {E_CompassMarkType::Home, "Home"},
        {E_CompassMarkType::Shop, "Shop"},
        {E_CompassMarkType::Blacksmith, "Blacksmith"},
        {E_CompassMarkType::Armourer, "Armourer"},
        {E_CompassMarkType::Weaponsmiths, "Weaponsmiths"},
        {E_CompassMarkType::Shoemaker, "Shoemaker"},
        {E_CompassMarkType::Tailor, "Tailor"},
        {E_CompassMarkType::Pub, "Pub"},
        {E_CompassMarkType::Apothecary, "Apothecary"},
        {E_CompassMarkType::Herbalist, "Herbalist"},
        {E_CompassMarkType::Miller, "Miller"},
        {E_CompassMarkType::HorseTrader, "HorseTrader"},
        {E_CompassMarkType::Saddler, "Saddler"},
        {E_CompassMarkType::Bailiff, "Bailiff"},
        {E_CompassMarkType::Arena, "Arena"},
        {E_CompassMarkType::ArcheryArena, "ArcheryArena"},
        {E_CompassMarkType::FistFight, "FistFight"},
        {E_CompassMarkType::Alchemy, "Alchemy"},
        {E_CompassMarkType::SharpeningWheel, "SharpeningWheel"},
        {E_CompassMarkType::Baths, "Baths"},
        {E_CompassMarkType::HuntingSpot, "HuntingSpot"},
        {E_CompassMarkType::HuntingSpotBoar, "HuntingSpotBoar"},
        {E_CompassMarkType::FishingSpot, "FishingSpot"},
        {E_CompassMarkType::Hive, "Hive"},
        {E_CompassMarkType::ForestGarden, "ForestGarden"},
        {E_CompassMarkType::Camp, "Camp"},
        {E_CompassMarkType::CampEnemy, "CampEnemy"},
        {E_CompassMarkType::MineEnrtrance, "MineEnrtrance"},
        {E_CompassMarkType::Grave, "Grave"},
        {E_CompassMarkType::Nest, "Nest"},
        {E_CompassMarkType::ConcCross, "ConcCross"},
        {E_CompassMarkType::Shrine, "Shrine"},
        {E_CompassMarkType::Bed, "Bed"},
        {E_CompassMarkType::GeneralPoi, "GeneralPoi"},
        {E_CompassMarkType::Bakery, "Bakery"},
        {E_CompassMarkType::Butchery, "Butchery"},
        {E_CompassMarkType::VegetableShop, "VegetableShop"},
        {E_CompassMarkType::Tanner, "Tanner"},
        {E_CompassMarkType::Hunter, "Hunter"},
        {E_CompassMarkType::Scribe, "Scribe"},
        {E_CompassMarkType::Hotel, "Hotel"},
        {E_CompassMarkType::HuntingSpotRoe, "HuntingSpotRoe"},
        {E_CompassMarkType::HuntingSpotWolf, "HuntingSpotWolf"},
        {E_CompassMarkType::Smithy, "Smithy"},
        {E_CompassMarkType::SkillTeacher, "SkillTeacher"},
        {E_CompassMarkType::FightArena, "FightArena"},
        {E_CompassMarkType::PoiTipster, "PoiTipster"},
        {E_CompassMarkType::SmokingFood, "SmokingFood"},
        {E_CompassMarkType::DryingFood, "DryingFood"},
        {E_CompassMarkType::Washing, "Washing"},
        {E_CompassMarkType::Indulgences, "Indulgences"},
        {E_CompassMarkType::Gunsmith, "Gunsmith"},
        {E_CompassMarkType::DiceTable, "DiceTable"},
        {E_CompassMarkType::FistFightArena, "FistFightArena"},
        {E_CompassMarkType::DLC0, "DLC0"},
        {E_CompassMarkType::BedPlayer, "BedPlayer"},
        {E_CompassMarkType::Barber, "Barber"},
        {E_CompassMarkType::ShieldPainter, "ShieldPainter"},
        {E_CompassMarkType::DLC1, "DLC1"},
        {E_CompassMarkType::DLC2, "DLC2"},
        {E_CompassMarkType::DLC3, "DLC3"},
        {E_CompassMarkType::Racing, "Racing"},
        {E_CompassMarkType::DLC2_smithing, "DLC2_smithing"},
        {E_CompassMarkType::DLC2_dice, "DLC2_dice"},
        {E_CompassMarkType::DLC2_acquiringPackages, "DLC2_acquiringPackages"},
        {E_CompassMarkType::DLC2_archery, "DLC2_archery"},
        {E_CompassMarkType::DLC2_donations, "DLC2_donations"},
        {E_CompassMarkType::DLC2_duels, "DLC2_duels"},
        {E_CompassMarkType::DLC2_stealingPackages, "DLC2_stealingPackages"},
        {E_CompassMarkType::DLC2_activities, "DLC2_activities"},
        {E_CompassMarkType::SellingChest, "SellingChest"},
        {E_CompassMarkType::FastTravelSedlec, "FastTravelSedlec"},
    };

    /**
     * The index a mark type occupies in the map's visibility table and legend,
     * which is not the type itself: the DLC icons collapse onto the umbrella
     * entry they share a row with. Always <= 0x60, so it is safe to index the
     * 0x61-entry table with.
     *
     * Transcribed from 0x180A1B1C4 rather than called: its five-byte prologue
     * is shared with hundreds of unrelated functions.
     */
    inline int NormalizeCompassMarkType(E_CompassMarkType type) {
        switch (type) {
        case E_CompassMarkType::DLC0:
        case E_CompassMarkType::DLC1:
        case E_CompassMarkType::DLC2:
        case E_CompassMarkType::DLC3: return static_cast<int>(E_CompassMarkType::Dlcs);

        case E_CompassMarkType::DLC2_smithing:
        case E_CompassMarkType::DLC2_dice:
        case E_CompassMarkType::DLC2_acquiringPackages:
        case E_CompassMarkType::DLC2_archery:
        case E_CompassMarkType::DLC2_donations:
        case E_CompassMarkType::DLC2_duels:
        case E_CompassMarkType::DLC2_stealingPackages: return static_cast<int>(E_CompassMarkType::DLC2_activities);

        default: return static_cast<int>(type);
        }
    }

    /** The flash icon name, or nullptr for a value the game does not map. */
    inline const char *CompassMarkTypeToIconName(E_CompassMarkType type) {
        for (const auto &row : kCompassMarkTypeNames) {
            if (row.type == type) {
                return row.name;
            }
        }
        return nullptr;
    }

    /** The type spelled this way, or nothing. Compared exactly: these are the binary's identifiers, misspellings included. */
    inline bool CompassMarkTypeFromIconName(const char *name, E_CompassMarkType &type) {
        if (name == nullptr) {
            return false;
        }

        for (const auto &row : kCompassMarkTypeNames) {
            const char *left  = row.name;
            const char *right = name;
            while (*left != 0 && *left == *right) {
                ++left;
                ++right;
            }
            if (*left == 0 && *right == 0) {
                type = row.type;
                return true;
            }
        }
        return false;
    }
} // namespace SDK::wh::guimodule
