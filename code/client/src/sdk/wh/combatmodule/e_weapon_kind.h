#pragma once

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Coarse class of what a fighter has in hand.
     *
     * Named and valued by the game's RTTR registration of
     * wh::combatmodule::E_WeaponKind at 0x1800C8F00. `none` is an empty hand;
     * `undefined` is a held item combat has no classification for, which is
     * not the same thing.
     */
    struct E_WeaponKind {
        enum Type : std::uint32_t {
            None      = 0,
            Melee     = 1,
            Missile   = 2,
            Oversized = 3,
            Undefined = 4,

            Count = 5,
        };

        static const char *Name(Type kind) {
            switch (kind) {
            case None: return "none";
            case Melee: return "melee";
            case Missile: return "missile";
            case Oversized: return "oversized";
            case Undefined: return "undefined";
            default: return "?";
            }
        }
    };

    /**
     * Where the blade IK solver is in its cycle after a contact.
     *
     * Named and valued by the RTTR registration of
     * wh::combatmodule::E_WeaponSolverState at 0x1800C0D70.
     */
    struct E_WeaponSolverState {
        enum Type : std::uint32_t {
            None    = 0,
            Recoil  = 1,
            Sliding = 2,
            Recover = 3,

            Count = 4,
        };

        static const char *Name(Type state) {
            switch (state) {
            case None: return "none";
            case Recoil: return "recoil";
            case Sliding: return "sliding";
            case Recover: return "recover";
            default: return "?";
            }
        }
    };

    /**
     * How the blade IK solver should behave on this contact.
     *
     * Named and valued by the RTTR registration of
     * wh::combatmodule::E_WeaponSolverMode at 0x1800C1550. The penetration
     * variants pair one-to-one with the plain ones.
     */
    struct E_WeaponSolverMode {
        enum Type : std::uint32_t {
            Normal                = 0,
            NormalWithPenetration = 1,
            Stuck                 = 2,
            StuckWithPenetration  = 3,
            Recoil                = 4,
            RecoilWithPenetration = 5,

            Count = 6,
        };

        static const char *Name(Type mode) {
            switch (mode) {
            case Normal: return "normal";
            case NormalWithPenetration: return "normalWithPenetration";
            case Stuck: return "stuck";
            case StuckWithPenetration: return "stuckWithPenetration";
            case Recoil: return "recoil";
            case RecoilWithPenetration: return "recoilWithPenetration";
            default: return "?";
            }
        }
    };

    /**
     * The seven weapon families the combo tables are keyed on.
     *
     * Named and valued by the RTTR registration of
     * wh::combatmodule::E_UIComboGroup at 0x1800C6B80. `C_CombatCombo` carries
     * one of these as its UIGroup, which is how the codex groups the moves a
     * player has learned.
     */
    struct E_UIComboGroup {
        enum Type : std::uint32_t {
            Longsword        = 0,
            Shortsword       = 1,
            ShortswordShield = 2,
            Blunt            = 3,
            BluntShield      = 4,
            Halberd          = 5,
            Unarmed          = 6,

            Count = 7,
        };

        static const char *Name(Type group) {
            switch (group) {
            case Longsword: return "Longsword";
            case Shortsword: return "Shortsword";
            case ShortswordShield: return "ShortswordShield";
            case Blunt: return "Blunt";
            case BluntShield: return "BluntShield";
            case Halberd: return "Halberd";
            case Unarmed: return "Unarmed";
            default: return "?";
            }
        }
    };

    /**
     * What a battle's roster did, as reported by C_BattleManager.
     *
     * Named and valued by the RTTR registration of
     * wh::combatmodule::E_BattleEvent at 0x1800AF870.
     */
    struct E_BattleEvent {
        enum Type : std::uint32_t {
            AddedSoul              = 0,
            RemovedSoul            = 1,
            DeadSoul               = 2,
            TransformationComplete = 3,

            Count = 4,
        };

        static const char *Name(Type event) {
            switch (event) {
            case AddedSoul: return "AddedSoul";
            case RemovedSoul: return "RemovedSoul";
            case DeadSoul: return "DeadSoul";
            case TransformationComplete: return "TransformationComplete";
            default: return "?";
            }
        }
    };

    static_assert(sizeof(E_WeaponKind::Type) == 0x4, "E_WeaponKind::Type size mismatch");
    static_assert(sizeof(E_WeaponSolverState::Type) == 0x4, "E_WeaponSolverState::Type size mismatch");
    static_assert(sizeof(E_WeaponSolverMode::Type) == 0x4, "E_WeaponSolverMode::Type size mismatch");
    static_assert(sizeof(E_UIComboGroup::Type) == 0x4, "E_UIComboGroup::Type size mismatch");
    static_assert(sizeof(E_BattleEvent::Type) == 0x4, "E_BattleEvent::Type size mismatch");
} // namespace SDK::wh::combatmodule
