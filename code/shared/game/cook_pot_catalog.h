#pragma once

#include "shared/features/player/player_buff.h"
#include <cstdint>
#include <glm/vec3.hpp>
#include <span>
#include <string_view>

namespace KCDC::Shared::Game {
    enum class CookPotFood : std::uint8_t {
        Goulash,
        Lentil,
        Soup
    };

    inline constexpr std::string_view CookPotFoodName(CookPotFood food) {
        switch (food) {
        case CookPotFood::Goulash: return "goulash";
        case CookPotFood::Lentil: return "lentil";
        case CookPotFood::Soup: return "soup";
        }
        return {};
    }

    struct CookPotPoison {
        std::string_view item;
        KCDC::Shared::RPC::BuffId buff;
    };
    /** Ingested effects of Poison items, never their weapon-coating effects. */
    extern const std::span<const CookPotPoison> g_CookPotPoisons;
    extern const std::uint16_t g_CookPotNutrition[3];

    struct CookPotPlacement {
        const char *level;
        std::uint64_t guid;
        std::uint64_t triggerGuid;
        std::uint64_t cauldronGuid;
        glm::vec3 position;
        CookPotFood food;
        bool home;
        bool fullOnInit;
        /** The fireplace's `proceduralObject['fire']` and `['steam']` prefabs, switched together by so_fireplace.xml. */
        std::uint64_t fireGuid;
        std::uint64_t steamGuid;
    };

    /** An item class as the engine's CryGUID halves. */
    struct CookPotItemClass {
        std::uint64_t hipart;
        std::uint64_t lopart;
    };

    /** What a pot of each food feeds, by `CookPotFood`: `FireplaceSmartObject:GetEatItemClassId`. */
    extern const CookPotItemClass g_CookPotFoodItemClasses[3];

    extern const std::span<const CookPotPlacement> g_CookPotPlacements;
} // namespace KCDC::Shared::Game
