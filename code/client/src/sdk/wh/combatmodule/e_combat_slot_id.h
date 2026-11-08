#pragma once

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * The defensive reaction a fighter's slot planner has reserved.
     *
     * Named and valued by the game's RTTR registration of
     * wh::combatmodule::E_CombatSlotId at 0x1800C9720. These are the reactions
     * C_CombatActorSlotPlanner schedules against an incoming attack; the
     * matching C_CombatTrigger and C_CombatMultiTrigger classes exist one per
     * enumerator, which is what proves the list is the whole set.
     *
     * This is not an attack direction. The combat star's directions are the
     * `combat_zone` tables, carried as zone ids.
     */
    struct E_CombatSlotId {
        enum Type : std::uint32_t {
            Undefined        = 0,
            PerfectBlock     = 1,
            Riposte          = 2,
            Combo            = 3,
            SyncPerfectBlock = 4,
            MasterStrike     = 5,
            Block            = 6,
            Dodge            = 7,

            Count = 8,
        };

        static const char *Name(Type slot) {
            switch (slot) {
            case Undefined: return "Undefined";
            case PerfectBlock: return "PerfectBlock";
            case Riposte: return "Riposte";
            case Combo: return "Combo";
            case SyncPerfectBlock: return "SyncPerfectBlock";
            case MasterStrike: return "MasterStrike";
            case Block: return "Block";
            case Dodge: return "Dodge";
            default: return "Unknown";
            }
        }
    };

    static_assert(sizeof(E_CombatSlotId::Type) == 0x4, "E_CombatSlotId::Type size mismatch");
} // namespace SDK::wh::combatmodule
