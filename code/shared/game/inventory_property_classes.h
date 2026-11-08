#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** Native class constraints, including aliases and inherited armor types. */
    struct InventoryPropertyClass {
        std::string_view guid;
        std::uint32_t maximumQuality;
        bool canHealthChange;
        bool equippable;
        bool breakableWeapon;
        // PlayerItem/ItemAlias::IsQuestItem: C_Inventory::CreateItems makes none, only its quest hands one out.
        bool questItem;
        // Native class slot 7: CreateItems preserves a quantity in one C_Item.
        bool nativeStackable;
    };

    extern const std::span<const InventoryPropertyClass> g_InventoryPropertyClasses;
    const InventoryPropertyClass *FindInventoryPropertyClass(std::string_view guid);
    /** The compiled table alone, without the custom items or the streamed mods' classes. */
    const InventoryPropertyClass *FindShippedInventoryPropertyClass(std::string_view guid);
    /** Ointment Type=repairkit, including aliases, from the shipped tables. */
    extern const std::span<const std::string_view> g_RepairKitClasses;
    bool IsRepairKitClass(std::string_view guid);
    /** What a grindstone takes: melee weapons with sharpening zones, not training or stub ones. */
    extern const std::span<const std::string_view> g_SharpenableClasses;
    bool IsSharpenableClass(std::string_view guid);
} // namespace KCDC::Shared::Game
