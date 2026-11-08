#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * The authored numbers of an equippable item class a server may override.
     *
     * KCD2 keeps every combat number on the class row, never on the item: a
     * `C_Item` carries only health and quality, and the hit calculation reads
     * attack and defense from the `S_*ItemClass` behind it on every hit. The
     * reflection blocks of `item.xml`'s element types name these fields, and
     * their offsets are proven in the client SDK (`s_melee_weapon_item_class.h`
     * and siblings). Which stat a class has depends on its table element,
     * which `ItemClassEntry::category` records.
     */
    enum class ItemStat : std::uint8_t {
        // MeleeWeapon
        Attack,
        AttackModStab,
        AttackModSlash,
        AttackModSmash,
        // MeleeWeapon and MissileWeapon
        Defense,
        StrReq,
        AgiReq,
        // Armor, Helmet, Hood
        DefenseStab,
        DefenseSlash,
        DefenseSmash,
        Noise,
        // MissileWeapon
        Power,
        LimbResistance,
        // Ammo
        AttackStab,
        AttackSlash,
        AttackSmash,
        PowerMod,
        Count
    };

    inline constexpr std::size_t kItemStatCount = static_cast<std::size_t>(ItemStat::Count);

    /** The scripting spelling of each stat, which is the table's attribute with a lower-case initial. */
    inline constexpr std::array<std::string_view, kItemStatCount> kItemStatNames = {
        "attack", "attackModStab", "attackModSlash", "attackModSmash", "defense", "strReq", "agiReq", "defenseStab", "defenseSlash", "defenseSmash", "noise", "power", "limbResistance", "attackStab", "attackSlash", "attackSmash", "powerMod"};

    inline constexpr std::string_view ItemStatName(ItemStat stat) {
        return static_cast<std::size_t>(stat) < kItemStatCount ? kItemStatNames[static_cast<std::size_t>(stat)] : std::string_view();
    }

    /** -1 for a name no stat carries. */
    inline constexpr int FindItemStat(std::string_view name) {
        for (std::size_t i = 0; i < kItemStatCount; ++i) {
            if (kItemStatNames[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /** Whether a class of this table element has this stat at all. */
    inline constexpr bool ItemStatAppliesTo(std::string_view category, ItemStat stat) {
        const bool melee   = category == "MeleeWeapon";
        const bool missile = category == "MissileWeapon";
        const bool armor   = category == "Armor" || category == "Helmet" || category == "Hood";
        const bool ammo    = category == "Ammo";
        switch (stat) {
        case ItemStat::Attack:
        case ItemStat::AttackModStab:
        case ItemStat::AttackModSlash:
        case ItemStat::AttackModSmash:
            return melee;
        case ItemStat::Defense:
        case ItemStat::AgiReq:
            return melee || missile;
        case ItemStat::StrReq:
            return melee || missile || armor;
        case ItemStat::DefenseStab:
        case ItemStat::DefenseSlash:
        case ItemStat::DefenseSmash:
        case ItemStat::Noise:
            return armor;
        case ItemStat::Power:
        case ItemStat::LimbResistance:
            return missile;
        case ItemStat::AttackStab:
        case ItemStat::AttackSlash:
        case ItemStat::AttackSmash:
        case ItemStat::PowerMod:
            return ammo;
        case ItemStat::Count:
            break;
        }
        return false;
    }

    /** Whether any stat at all can be overridden on a class of this element. */
    inline constexpr bool ItemCategoryHasStats(std::string_view category) {
        return category == "MeleeWeapon" || category == "MissileWeapon" || category == "Armor" || category == "Helmet" || category == "Hood" || category == "Ammo";
    }

    /** The requirement stats are whole stat levels in the tables; everything else is a float. */
    inline constexpr bool ItemStatIsIntegral(ItemStat stat) {
        return stat == ItemStat::StrReq || stat == ItemStat::AgiReq;
    }

    /** The bound every override must sit inside: the authored tables never leave it, and the hit formulas are linear in these. */
    inline constexpr float kItemStatLimit = 100000.0F;

    /**
     * Whether a value may be written as `stat`: finite, inside the bound, and
     * never below zero for a requirement -- a weapon holds its requirements as
     * unsigned integers, where -1 would read as four billion.
     */
    inline bool ItemStatAccepts(ItemStat stat, float value) {
        return std::isfinite(value) && std::fabs(value) <= kItemStatLimit && (!ItemStatIsIntegral(stat) || value >= 0.0F);
    }
} // namespace KCDC::Shared::Game
