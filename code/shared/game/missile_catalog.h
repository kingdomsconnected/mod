#pragma once

#include "shared/item_stack.h"
#include "shared/features/player/player_missile.h"

#include <vector>

namespace KCDC::Shared::Game {
    struct MissileClassEntry {
        RPC::ItemClassId itemClass;

        // Weapon: accepted ammo bits; ammunition: its single type bit.
        std::uint32_t ammunitionMask;
        bool weapon;
        bool requiresCharging;
        bool requiresCocking;
    };

    extern const std::vector<MissileClassEntry> g_MissileClasses;
    extern const std::vector<std::int32_t> g_MissileHumanBodyMaterials;

    struct MissilePoisonEntry {
        RPC::ItemClassId buff;
        std::uint32_t maxWeaponCharges;
    };

    extern const std::vector<MissilePoisonEntry> g_MissilePoisons;

    inline bool IsValidMissileItemProperties(const RPC::MissileItemProperties &properties, bool ammunition) {
        if (!properties.IsSane()) {
            return false;
        }

        if (!properties.poisonBuff.IsValid()) {
            return true;
        }

        for (const auto &poison : g_MissilePoisons) {
            if (poison.buff == properties.poisonBuff) {
                return properties.poisonCharges <= (ammunition ? 1U : poison.maxWeaponCharges);
            }
        }

        return false;
    }

    inline bool IsValidMissileLaunchProperties(const RPC::MissileLaunched &launch) {
        return IsValidMissileItemProperties(launch.weaponProperties, false) && IsValidMissileItemProperties(launch.ammunitionProperties, true);
    }

    inline bool IsValidRetainedMissileProperties(const RPC::MissileItemProperties &retained, const RPC::MissileItemProperties &launched) {
        return IsValidMissileItemProperties(retained, true) && retained.quality <= launched.quality && retained.health <= launched.health + 0.0001F && retained.poisonCharges <= launched.poisonCharges && (!retained.poisonBuff.IsValid() || retained.poisonBuff == launched.poisonBuff);
    }

    inline bool IsHumanMissileBodyMaterial(std::int32_t material) {
        if (material == -1) {
            return true; // Unknown material.
        }

        for (const auto value : g_MissileHumanBodyMaterials) {
            if (material == value) {
                return true;
            }
        }

        return false;
    }

    inline const MissileClassEntry *FindMissileClass(const RPC::ItemClassId &itemClass) {
        for (const auto &entry : g_MissileClasses) {
            if (entry.itemClass == itemClass) {
                return &entry;
            }
        }

        return nullptr;
    }

    inline bool IsCompatibleMissileAmmunition(const RPC::ItemClassId &weapon, const RPC::ItemClassId &ammunition) {
        const auto *w = FindMissileClass(weapon);
        const auto *a = FindMissileClass(ammunition);

        return w != nullptr && w->weapon && a != nullptr && !a->weapon && (w->ammunitionMask & a->ammunitionMask) != 0;
    }

    inline bool IsValidMissileGroundItem(const RPC::InventoryItem &item) {
        const auto *entry = FindMissileClass(item.itemClass);

        if (entry == nullptr) {
            return !item.poisonBuff.IsValid() && item.poisonCharges == 0;
        }

        return IsValidMissileItemProperties({item.quality, item.health, item.poisonBuff, item.poisonCharges}, !entry->weapon);
    }
} // namespace KCDC::Shared::Game
