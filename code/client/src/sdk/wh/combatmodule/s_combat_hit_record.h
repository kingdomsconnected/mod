#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../framework/wuid.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * What one blow did, as the victim's body is told about it.
     *
     * Built by the constructor at 0x180B2454C and filled per hit entry by
     * `FillCombatHitRecord` (0x180725DAC). The collision handler hands it to
     * `CGameRules::QueueHitRecord` (slot 165); `FlushHitRecords` delivers it
     * to the victim's game object as event 32. The victim's handler then plays
     * the blow's impact -- the clash or armour sound, the sparks, the blood --
     * as one CryAction material effect, `PlayHitMaterialEffect`, keyed on
     * this record alone.
     *
     * Only the fields that effect reads are named.
     * `C_RPGUtils::BuildHitMaterialEffectName` (slot 28) reads the effect
     * row's name off them, and the surface ids pick its column.
     */
    struct S_CombatHitRecord {
        /** The flag in `m_presentationFlags` under which the victim's handler plays the impact. */
        static constexpr std::uint32_t kPlaysMaterialEffect = 0x4;

        /**
         * The flag in `m_presentationFlags` under which the victim's handler
         * (`ProcessActorHitRecord`, 0x180B24600) also tries the body's own
         * `hitreaction` fragment. Cleared, the handler counts the reaction as
         * played; set, a body that is dead and plays none is given its dead
         * action instead.
         */
        static constexpr std::uint32_t kRunsHitReaction = 0x2;

        /**
         * The hit type a thrown decoy's contact queues:
         * C_Decoy::QueueHit182ACD0A4 sets it, and sub_180B24964 picks its
         * reaction class from it. No wider enum is proven.
         */
        static constexpr std::int32_t kDecoyHit = 8;

        /** A part id the record carries none of. */
        static constexpr std::int32_t kNoPart = -1;

        /** A record holding the engine's defaults, exactly as its own constructor writes them. */
        static S_CombatHitRecord Make() {
            S_CombatHitRecord record;
            hook::this_call<void>(gPatterns.S_CombatHitRecord__Construct, &record);
            return record;
        }

        bool PlaysMaterialEffect() const {
            return (m_presentationFlags & kPlaysMaterialEffect) != 0;
        }

        /**
         * Plays this blow's impact where the record puts it, as the victim's
         * hit handler does. Fire and forget: the effect owns its sound and
         * particles from here on.
         */
        bool PlayMaterialEffect() const {
            return hook::call<bool>(gPatterns.CombatHitRecord__PlayMaterialEffect, nullptr, this);
        }

        std::uint32_t m_attackerEntityId; // 0x00
        std::byte m_padding0004[0x4];     // 0x04, never written: alignment for the WUID

        /** The weapon the blow was struck with; the effect row is named after its class. */
        framework::WUID m_weaponItemId; // 0x08

        /** The weapon's kind: 1..3 armed, 3 also a missile. Unarmed answers a blunt row. */
        std::int32_t m_weaponKind; // 0x10

        /** The striking surface; the fallback column, and the wooden-weapon test. */
        std::int32_t m_sourceSurfaceId; // 0x14

        std::int32_t m_hitType;      // 0x18
        std::int32_t m_ammoClass;    // 0x1C
        std::int32_t m_attackStyle;   // 0x20, 1 = stab
        std::uint8_t m_value0024;     // 0x24, 2 by default; the attacker model +0x150 value mapped by 0x180726790
        std::byte m_padding0025[0x3]; // 0x25

        std::uint32_t m_victimEntityId; // 0x28
        float m_staminaDamage;          // 0x2C

        /** Health the blow took; any at all makes it a `passed` row rather than a `stopped` one. */
        float m_healthDamage; // 0x30

        /** The body material struck; resolved against the victim's soul to the armour layer it hit. */
        std::int32_t m_bodyMaterialId; // 0x34

        /** The struck surface: the effect's column. */
        std::int32_t m_targetSurfaceId; // 0x38

        /** The struck part, used when `m_jointId` names none. */
        std::int32_t m_partId;        // 0x3C
        std::uint8_t m_flag0040;      // 0x40, false by default; 0x180726440 per hit entry

        /**
         * Whether the blow knocked the victim out: its event carries the
         * game's `unconscious` buff (f8d60fe4, 0x185582F90) for the victim's
         * soul -- `C_RPGCombat` slot 49 (0x18072647C), filled per hit entry
         * through 0x180726400. `ProcessActorHitRecord` then picks the
         * reaction class it picks for a kill (0x180B24964).
         */
        std::uint8_t m_knocksOutVictim; // 0x41
        std::byte m_padding0042[0x2];   // 0x42
        std::int32_t m_subpartId;       // 0x44, 0 by default; 0x1807263C4 per hit entry

        /** The entity `m_jointId` belongs to -- the attacker, when the contact named no part. */
        std::uint32_t m_jointOwnerEntityId; // 0x48
        std::int32_t m_jointId;             // 0x4C

        Vec3 m_position;             // 0x50
        Vec3 m_normal;               // 0x5C
        Vec3 m_direction;             // 0x68
        std::uint8_t m_flag0074;      // 0x74, false by default
        std::uint8_t m_flag0075;      // 0x75, false by default
        std::uint8_t m_flag0076;      // 0x76, false by default
        std::byte m_padding0077[0x1]; // 0x77

        /** Constructed 0x3F; the collision handler clears bits it does not want. */
        std::uint32_t m_presentationFlags; // 0x78

        /**
         * The class id of the item each hand holds, 0 then 1, -1 for none:
         * 0x180726114 reads the handled item's descriptor. The attacker's
         * pair is filled once per record, the victim's per hit entry.
         */
        std::int32_t m_attackerHandItemClass[2]; // 0x7C
        std::int32_t m_victimHandItemClass[2];   // 0x84
        std::uint32_t m_values008C[4];           // 0x8C, zero (xmmword 0x18409F260)
        std::byte m_padding009C[0x4];            // 0x9C, never written

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CombatHitRecord, m_attackerEntityId) == 0x00, "S_CombatHitRecord::m_attackerEntityId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_weaponItemId) == 0x08, "S_CombatHitRecord::m_weaponItemId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_weaponKind) == 0x10, "S_CombatHitRecord::m_weaponKind offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_sourceSurfaceId) == 0x14, "S_CombatHitRecord::m_sourceSurfaceId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_hitType) == 0x18, "S_CombatHitRecord::m_hitType offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_ammoClass) == 0x1C, "S_CombatHitRecord::m_ammoClass offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_attackStyle) == 0x20, "S_CombatHitRecord::m_attackStyle offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_victimEntityId) == 0x28, "S_CombatHitRecord::m_victimEntityId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_staminaDamage) == 0x2C, "S_CombatHitRecord::m_staminaDamage offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_healthDamage) == 0x30, "S_CombatHitRecord::m_healthDamage offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_bodyMaterialId) == 0x34, "S_CombatHitRecord::m_bodyMaterialId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_targetSurfaceId) == 0x38, "S_CombatHitRecord::m_targetSurfaceId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_partId) == 0x3C, "S_CombatHitRecord::m_partId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_jointOwnerEntityId) == 0x48, "S_CombatHitRecord::m_jointOwnerEntityId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_jointId) == 0x4C, "S_CombatHitRecord::m_jointId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_position) == 0x50, "S_CombatHitRecord::m_position offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_normal) == 0x5C, "S_CombatHitRecord::m_normal offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_direction) == 0x68, "S_CombatHitRecord::m_direction offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_presentationFlags) == 0x78, "S_CombatHitRecord::m_presentationFlags offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_value0024) == 0x24, "S_CombatHitRecord::m_value0024 offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_flag0040) == 0x40, "S_CombatHitRecord::m_flag0040 offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_knocksOutVictim) == 0x41, "S_CombatHitRecord::m_knocksOutVictim offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_subpartId) == 0x44, "S_CombatHitRecord::m_subpartId offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_flag0074) == 0x74, "S_CombatHitRecord::m_flag0074 offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_attackerHandItemClass) == 0x7C, "S_CombatHitRecord::m_attackerHandItemClass offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_victimHandItemClass) == 0x84, "S_CombatHitRecord::m_victimHandItemClass offset mismatch");
            static_assert(offsetof(S_CombatHitRecord, m_values008C) == 0x8C, "S_CombatHitRecord::m_values008C offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatHitRecord) == 0xA0, "S_CombatHitRecord size mismatch");
} // namespace SDK::wh::combatmodule
