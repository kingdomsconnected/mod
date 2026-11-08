#pragma once

#include "s_missile_hit_data.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * One melee blow as the RPG side is handed it: what
     * `BuildAndRaiseCombatHit` (0x180D568B0) fills once per hit entry and
     * passes to `C_CombatSoul::ProcessCombatHit` (0x180726DF4), 0x90 bytes.
     *
     * The size is the cause's: `C_EventFactory::CreateCombatHitEvent`
     * (0x180726F08) allocates a 0x98-byte `C_CombatHitCause` and copies nine
     * 16-byte blocks of this to +0x08. The fields named here are the ones
     * `C_HitCalculation::CopyHitData` (0x180727B0C) reads and
     * `ResolveVictimDamageEffects` (0x180C63E0C) branches on; the flag bytes
     * at +0x54.. are copied whole to the calculation's +0x90, where +0x92 --
     * this +0x56 -- zeroes both the health and the stamina of a perfect block.
     * The block tests read the calculation's +0x90 (blocked, in
     * `C_HitCalculation::GetBlockDefense` 0x180728170), +0x91 (weak) and
     * +0x93 (broken, in the stab pass 0x180C65B78).
     */
    struct S_CombatHitData {
        S_SoulEntityIdentity180D571E4 attacker; // 0x00
        S_SoulEntityIdentity180D571E4 victim;   // 0x10

        /** A `combat_attack_type` row id. */
        std::int32_t attackTypeId; // 0x20

        /** Stab 1, slash 2, smash 4: the passes the calculation runs. */
        std::uint32_t damageTypes; // 0x24
        /** BuildAndRaiseCombatHit 0x180D568B0: the attacker combat component's (+0x150) vtable slot 1 result. */
        std::int32_t m_value0028; // 0x28

        /** The attacker's stamina coefficient for this swing, 0..1. */
        float staminaCoefficient; // 0x2C

        /** The contact's energy, 0..1, or 1 where the contact was forced. */
        float energy; // 0x30

        /**
         * The blade's velocity against the victim's, world axes. Its length
         * scales a mounted blow (`GetMountedAttackCoef` 0x180727F98, read
         * at the calculation's +0x40).
         */
        float relativeVelocity[3]; // 0x34
        /** Twelve bytes copied whole from one source record in BuildAndRaiseCombatHit; meaning unproven. */
        float m_vector0040[3]; // 0x40

        /** The hit entry's +0x68 dword. */
        std::int32_t m_value004C; // 0x4C
        std::int32_t m_value0050; // 0x50

        /** The victim's block caught it: a parry in place, or blade on blade. */
        bool blocked; // 0x54

        /** The block was the weaker hand's; the stamina it costs doubles. */
        bool weakBlock; // 0x55

        /** The victim's perfect block was running: no health and no stamina are taken. */
        bool perfectBlock; // 0x56

        /** `IsBlockBrokenHitAction`: the block gave way. */
        bool blockBroken; // 0x57
        /** Six further flag bytes BuildAndRaiseCombatHit fills from the contact and the combat flags; meanings unproven. */
        bool m_flags0058[6]; // 0x58

        /** The attack is a counter: the attacker's `cat` modifiers raise each pass (0x180C6677C, the calculation's +0x9A). */
        bool counterAttack; // 0x5E

        /** Struck from the saddle: the attack scales with the horse's speed (the calculation's +0x9B). */
        bool mounted; // 0x5F
        /** Set when two compared contact values are equal (BuildAndRaiseCombatHit's `v38 == v37`). */
        bool m_flag0060; // 0x60

        /** The next five bytes are written from the attacker's C_Actor combat flags (HasCombatFlags 0x180688B44); meanings unproven. */
        bool m_flags0061[5]; // 0x61
        std::byte m_padding0066[0x2];

        /** A `body_subpart.xml` id, or -1. */
        std::int32_t hitSubpart; // 0x68

        /** The hit entry's own attack coefficient. */
        float attackCoefficient; // 0x6C

        /** The angle from the victim to the attacker, radians. */
        float angle; // 0x70
        /** The hit entry's +0x04 dword. */
        std::int32_t m_value0074; // 0x74

        /** The attacker actor, or an object resolved through the attacker's +0x118 container. */
        const void *m_ptr0078; // 0x78
        std::uint64_t m_value0080; // 0x80
        /** Defaults to qword_185321990 when the source has none. */
        std::uint64_t m_value0088; // 0x88

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CombatHitData, attacker) == 0x00, "S_CombatHitData::attacker offset mismatch");
            static_assert(offsetof(S_CombatHitData, victim) == 0x10, "S_CombatHitData::victim offset mismatch");
            static_assert(offsetof(S_CombatHitData, attackTypeId) == 0x20, "S_CombatHitData::attackTypeId offset mismatch");
            static_assert(offsetof(S_CombatHitData, damageTypes) == 0x24, "S_CombatHitData::damageTypes offset mismatch");
            static_assert(offsetof(S_CombatHitData, staminaCoefficient) == 0x2C, "S_CombatHitData::staminaCoefficient offset mismatch");
            static_assert(offsetof(S_CombatHitData, energy) == 0x30, "S_CombatHitData::energy offset mismatch");
            static_assert(offsetof(S_CombatHitData, relativeVelocity) == 0x34, "S_CombatHitData::relativeVelocity offset mismatch");
            static_assert(offsetof(S_CombatHitData, blocked) == 0x54, "S_CombatHitData::blocked offset mismatch");
            static_assert(offsetof(S_CombatHitData, weakBlock) == 0x55, "S_CombatHitData::weakBlock offset mismatch");
            static_assert(offsetof(S_CombatHitData, perfectBlock) == 0x56, "S_CombatHitData::perfectBlock offset mismatch");
            static_assert(offsetof(S_CombatHitData, blockBroken) == 0x57, "S_CombatHitData::blockBroken offset mismatch");
            static_assert(offsetof(S_CombatHitData, counterAttack) == 0x5E, "S_CombatHitData::counterAttack offset mismatch");
            static_assert(offsetof(S_CombatHitData, mounted) == 0x5F, "S_CombatHitData::mounted offset mismatch");
            static_assert(offsetof(S_CombatHitData, hitSubpart) == 0x68, "S_CombatHitData::hitSubpart offset mismatch");
            static_assert(offsetof(S_CombatHitData, attackCoefficient) == 0x6C, "S_CombatHitData::attackCoefficient offset mismatch");
            static_assert(offsetof(S_CombatHitData, angle) == 0x70, "S_CombatHitData::angle offset mismatch");
            static_assert(offsetof(S_CombatHitData, m_vector0040) == 0x40, "S_CombatHitData::m_vector0040 offset mismatch");
            static_assert(offsetof(S_CombatHitData, m_flags0058) == 0x58, "S_CombatHitData::m_flags0058 offset mismatch");
            static_assert(offsetof(S_CombatHitData, m_flags0061) == 0x61, "S_CombatHitData::m_flags0061 offset mismatch");
            static_assert(offsetof(S_CombatHitData, m_ptr0078) == 0x78, "S_CombatHitData::m_ptr0078 offset mismatch");
            static_assert(offsetof(S_CombatHitData, m_value0088) == 0x88, "S_CombatHitData::m_value0088 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatHitData) == 0x90, "S_CombatHitData size mismatch");
} // namespace SDK::wh::rpgmodule
