#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    enum class CraftKind : std::uint8_t {
        Alchemy,
        Smithing
    };

    struct CraftMaterial {
        std::string_view item;
        std::uint32_t amount;
    };

    struct CraftProduct {
        std::string_view item;
        std::uint32_t threshold;
        std::uint32_t maxYield;
    };

    struct CraftRecipe {
        CraftKind kind;
        std::string_view id;
        std::string_view name;
        std::uint8_t dlc;
        std::uint32_t minSkill;
        std::string_view perk;
        std::string_view base;
        std::vector<CraftMaterial> ingredients;
        std::vector<CraftProduct> products;
    };

    struct CraftStation {
        std::string_view id;
        std::string_view level;
        std::uint64_t guid;
        CraftKind kind;
        std::string_view layer;
        float x, y, z;
        float forwardX, forwardY;
    };

    /** An item slot a shipped station links: a recipe book on an alchemy stand, the tongs or hammer of a smithery. */
    struct CraftStationSlot {
        std::string_view station;
        std::uint64_t slotGuid;
        std::uint64_t itemClassLow;
        std::uint64_t itemClassHigh;
    };

    /** A placed grindstone: where the native sharpening minigame can run. */
    struct SharpeningStation {
        std::string_view level;
        std::uint64_t guid;
        float x, y, z;
        /**
         * The wheel's `Properties.fSpeedMultiplier`, which
         * `C_Sharpening::SetGrindstone` (0x182EB0204) stores and
         * `SetGrindstoneAnimationSpeed` (0x182EB0888) scales the spinning
         * clip's playback by. Its sign is the wheel's direction.
         */
        float speedMultiplier;
    };

    /** What a drying rack, a smokehouse or a fire does to the items a player puts to it. */
    enum class PreparationKind : std::uint8_t {
        Drying,
        Smoking,
        Cooking
    };

    /**
     * A placed drying rack or smokehouse: an `so_smokehouse` smart object,
     * whose `isSmokehouse` script property says which.
     */
    struct PreparationStation {
        std::string_view id; // level:guid, as a CraftStation's
        std::string_view level;
        PreparationKind kind;
        float x, y, z;
    };

    /** The class drying, smoking or cooking makes of an item: its DriedItemId, SmokedItemId or CookedItemId. */
    struct PreparedItem {
        std::string_view item;
        PreparationKind kind;
        std::string_view product;
        // A dried herb comes out at full health; dried, smoked or cooked food keeps its own.
        bool herb;
    };

    /** How native smithing settles one recipe material. */
    struct SmithingMaterialRule {
        std::string_view item;
        // Required but never spent, on success or failure.
        bool questItem;
        // Only a divisible material loses units to a failed attempt.
        bool divisible;
    };

    enum class SmithingProductKind : std::uint8_t {
        MeleeWeapon,
        Armor,
        MiscItem
    };

    /** A smithing product's item class, as the native outcome treats it. */
    struct SmithingProductClass {
        std::string_view item;
        SmithingProductKind kind;
        // The native outcome creates no quest product; its quest does.
        bool questItem;
        // The authored MeleeWeapon Class, -1 for anything else.
        std::int32_t weaponClass;
    };

    extern const std::vector<CraftRecipe> g_CraftRecipes;
    extern const std::vector<CraftStation> g_CraftStations;
    extern const std::vector<CraftStationSlot> g_CraftStationBooks;
    /** Every item slot a smithery links: the tongs and hammer the minigame hands the smith. */
    extern const std::vector<CraftStationSlot> g_CraftStationTools;
    extern const std::vector<SharpeningStation> g_SharpeningStations;
    /** The grindstone the level places with this GUID, or null. */
    const SharpeningStation *FindSharpeningStation(std::string_view level, std::uint64_t guid);
    extern const std::vector<PreparationStation> g_PreparationStations;
    extern const std::vector<PreparedItem> g_PreparedItems;
    const PreparedItem *FindPreparedItem(PreparationKind kind, std::string_view item);
    extern const std::string_view g_CraftCatalogHash;
    const CraftRecipe *FindCraftRecipe(CraftKind kind, std::string_view id);
    const CraftStation *FindCraftStation(std::string_view id);
    extern const std::vector<SmithingMaterialRule> g_SmithingMaterialRules;
    extern const std::vector<SmithingProductClass> g_SmithingProductClasses;
    const SmithingMaterialRule *FindSmithingMaterialRule(std::string_view item);
    const SmithingProductClass *FindSmithingProductClass(std::string_view item);
    bool CraftDLCEnabled(std::uint8_t dlc, std::uint16_t required);
} // namespace KCDC::Shared::Game
