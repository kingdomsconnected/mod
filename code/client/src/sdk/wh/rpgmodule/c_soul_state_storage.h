#pragma once

#include "../framework/wuid.h"
#include "c_perk_list.h"
#include "c_soul_state_effect.h"
#include "e_soul_state.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class C_Soul;

    /**
     * One level-and-progress entry of a soul's stat or skill table, as the
     * XP effects write it: whole levels, and XP into the next level in Q7
     * (whole XP times 128).
     */
    struct S_SoulProgressEntry {
        std::uint32_t level;
        std::uint32_t xpQ7;
    };

    static_assert(sizeof(S_SoulProgressEntry) == 0x8, "S_SoulProgressEntry size mismatch");
    static_assert(offsetof(S_SoulProgressEntry, level) == 0, "S_SoulProgressEntry level offset mismatch");
    static_assert(offsetof(S_SoulProgressEntry, xpQ7) == 4, "S_SoulProgressEntry XP offset mismatch");

    /**
     * A soul's skill table, by E_SoulSkill, with its owner behind it; original
     * type name unknown. `C_SkillXPEffect::ApplyToSkillTable` (0x180C657FC)
     * and `NotifySkillChanged` (0x18046A9A0) both take it as their table.
     */
    struct S_SoulSkillTable {
        static constexpr std::size_t kCount = 35;

        S_SoulProgressEntry entries[kCount]; // 0x000
        C_Soul *soul;                        // 0x118

        /** Raises the skill's property group, then recomputes and raises every induced parent skill. */
        void NotifySkillChanged(std::uint32_t skill) {
            hook::call<void>(gPatterns.rpgmodule__NotifySkillChanged, this, skill);
        }
    };

    static_assert(offsetof(S_SoulSkillTable, soul) == 0x118, "S_SoulSkillTable::soul offset mismatch");
    static_assert(sizeof(S_SoulSkillTable) == 0x120, "S_SoulSkillTable size mismatch");

    // Constructor 0x18070F3E8; original type name unknown. Non-polymorphic,
    // 0x268 bytes, embedded at C_Soul+0x560 (active) and +0x840 (backup).
    class C_SoulStateStorage18070F3E8 {
      public:
        static constexpr std::size_t kStatCount  = 10;
        static constexpr std::size_t kSkillCount = S_SoulSkillTable::kCount;

        C_Soul *GetSoul() const {
            return m_pSoul;
        }
        float GetState(E_SoulState state) const {
            return m_states[static_cast<std::uint32_t>(state)];
        }
        // Human installs a complete snapshot before notifying any listeners.
        void SetReplicatedState(E_SoulState state, float value) {
            m_states[static_cast<std::uint32_t>(state)] = value;
        }

        /**
         * The writer that performs RPG death, revival and cause bookkeeping,
         * then notifies listeners. A puppet must never run it.
         *
         * Reviving goes through here rather than `C_Soul::SetState`, which
         * returns zero without writing for any health change on a soul that is
         * already dead: only this one has the dead-to-alive branch.
         */
        float SetState(E_SoulState state, float value, const C_SoulStateEffect *cause) {
            return hook::this_call<float>(gPatterns.C_SoulStateStorage__SetState, this, state, value, cause);
        }

        /**
         * A stat's level and progress, by E_SoulStat. `C_StatXPEffect::Apply`
         * writes exactly this entry (`soul + 0x56C + 8 * stat`) and nothing
         * else writes a stat in native gameplay. Absolute assignments below
         * also notify native property listeners; positive assignments finalize
         * an XP event before its deferred perk/UI dispatch.
         */
        const S_SoulProgressEntry &GetStatEntry(std::uint32_t stat) const {
            return m_stats[stat < kStatCount ? stat : 0];
        }

        /** A skill's, by E_SoulSkill; `C_SkillXPEffect::Apply` writes `soul + 0x5E8 + 8 * skill`. */
        const S_SoulProgressEntry &GetSkillEntry(std::uint32_t skill) const {
            return m_skills.entries[skill < kSkillCount ? skill : 0];
        }

        /** Absolute restoration uses the same typed records the XP effects write. */
        void SetStatProgress(std::uint32_t stat, const S_SoulProgressEntry &entry) {
            if (stat < kStatCount) {
                m_stats[stat] = entry;
            }
        }
        void SetSkillProgress(std::uint32_t skill, const S_SoulProgressEntry &entry) {
            if (skill >= kSkillCount) {
                return;
            }
            m_skills.entries[skill] = entry;
            m_skills.NotifySkillChanged(skill);
        }

        /** C_Soul::Butcher sets bit 1; RPGInventory::OnButcher owns bit 0. */
        bool IsButchered() const {
            return (m_harvestFlags & 2U) != 0;
        }
        void ClearButchered() {
            m_harvestFlags &= ~2U;
        }

        C_PerkList *GetPerkList() {
            return &m_perkList;
        }
        const C_PerkList *GetPerkList() const {
            return &m_perkList;
        }

      private:
        C_Soul *m_pSoul; // 0x000
        bool m_flag0008;                         // 0x008; ctor 0x18070F3E8 writes 1.
        std::byte m_padding0009[0x3];
        S_SoulProgressEntry m_stats[kStatCount]; // 0x00C
        std::byte m_opaque005C[0x4];
        std::uint32_t m_value0060;               // 0x060; ctor 0.
        std::byte m_padding0064[0x4];
        framework::WUID m_wuid0068;              // 0x068; ctor writes WUID::kInvalid (qword_185332358).
        std::int64_t m_value0070;                // 0x070; ctor -1.
        std::int64_t m_value0078;                // 0x078; ctor -1.
        std::uint32_t m_harvestFlags; // 0x080; ctor 0.
        std::byte m_padding0084[0x4];
        S_SoulSkillTable m_skills; // 0x088, owner back-pointer at 0x1A0
        float m_states[6];         // 0x1A8
        void *m_ptr01C0;                           // 0x1C0; ctor 0.
        std::byte m_value01C8[0x10];               // 0x1C8; ctor copies the 16-byte constant xmmword_185583B38.
        std::byte m_slots01D8[4][0x8];             // 0x1D8; four 8-byte slots built by sub_18121E5E0; type unproven.
        C_PerkList m_perkList; // 0x1F8

        static void AssertLayout() {
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_pSoul) == 0x0, "Soul state owner offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_stats) == 0xC, "Soul stat table offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_skills) == 0x88, "Soul skill table offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_states) == 0x1A8, "Soul states offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_perkList) == 0x1F8, "Soul perk list offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_wuid0068) == 0x68, "Soul state storage m_wuid0068 offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_harvestFlags) == 0x80, "Soul state storage m_harvestFlags offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_ptr01C0) == 0x1C0, "Soul state storage m_ptr01C0 offset mismatch");
            static_assert(offsetof(C_SoulStateStorage18070F3E8, m_slots01D8) == 0x1D8, "Soul state storage m_slots01D8 offset mismatch");
        }
    };

    static_assert(sizeof(C_SoulStateStorage18070F3E8) == 0x268, "Soul state storage size mismatch");
} // namespace SDK::wh::rpgmodule
