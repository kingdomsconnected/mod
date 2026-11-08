#pragma once

#include "../../types.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace SDK::wh::rpgmodule {
    // Declared, not included: the perk list owns a pointer to this class and
    // includes this header, so the two point at each other.
    class C_PerkList;

    /**
     * The perk-point banks of a soul that learns perks, and the one function
     * that spends them.
     *
     * Reached through `C_PerkList+0x58` (= `C_Soul+0x7B0`), which is null on a
     * soul that never learns -- only `soul__player.xml` sets
     * `enable_perk_point_gain`. The class name is reconstructed; nothing in
     * the binary names it.
     *
     * One u16 bank per tree: per stat at +0x10, per skill at +0x24, and the
     * main level (property 89) at +0x6A, which `ClearPerkPoints1811240E8`
     * zeroes as one run. `C_PerkList::EnableLearning180C3F09C` allocates the
     * object at 0x70 bytes and writes this list at +0x08. Only ever reached
     * through that pointer.
     */
    class C_PerkLearningState {
      public:
        static constexpr std::size_t kStatBanks  = 10;
        static constexpr std::size_t kSkillBanks = 35;

        C_PerkList *GetPerkList() const {
            return m_pPerkList;
        }

        std::uint16_t GetStatPoints(std::uint32_t stat) const {
            return stat < kStatBanks ? m_statPoints[stat] : 0;
        }

        std::uint16_t GetSkillPoints(std::uint32_t skill) const {
            return skill < kSkillBanks ? m_skillPoints[skill] : 0;
        }

        std::uint16_t GetMainPoints() const {
            return m_mainPoints;
        }

        /** Every bank in one copy -- stats, skills, then the main level -- for telling whether a call moved any. */
        std::array<std::uint16_t, kStatBanks + kSkillBanks + 1> GetAllPoints() const {
            std::array<std::uint16_t, kStatBanks + kSkillBanks + 1> points {};
            std::copy(std::begin(m_statPoints), std::end(m_statPoints), points.begin());
            std::copy(std::begin(m_skillPoints), std::end(m_skillPoints), points.begin() + kStatBanks);
            points.back() = m_mainPoints;
            return points;
        }

        /**
         * Writes a bank directly. The engine keeps no other copy -- the perk
         * screen and `LearnPerk` both read these -- so a write here is the
         * whole of a grant or a restore.
         */
        void SetStatPoints(std::uint32_t stat, std::uint16_t points) {
            if (stat < kStatBanks) {
                m_statPoints[stat] = points;
            }
        }

        void SetSkillPoints(std::uint32_t skill, std::uint16_t points) {
            if (skill < kSkillBanks) {
                m_skillPoints[skill] = points;
            }
        }

        void SetMainPoints(std::uint16_t points) {
            m_mainPoints = points;
        }

        /**
         * The perk screen's learn, at 0x182CC7EE0; its only caller is
         * `C_LearnPerkEffect::Apply`. Refuses a perk that is not in the
         * available list, picks the perk's bank from its row (stat selector,
         * else skill selector, else the main level), refuses an empty bank,
         * then decrements it and runs `C_PerkList::AddPerk` -- refunding the
         * point if the add fails.
         *
         * Its first instruction loads the GUID with `movaps`, so the argument
         * must be 16-byte aligned; every native caller passes an `__int128`.
         * A `CryGUID` is only 8-aligned, and handing one over as-is faulted.
         */
        bool LearnPerk(const CryGUID &perk) {
            alignas(16) const CryGUID aligned = perk;
            return hook::this_call<bool>(gPatterns.C_PerkLearningState__LearnPerk, this, &aligned);
        }

      private:
        std::byte m_opaque0000[0x8];              // byte 0 cleared on allocation; its use is unproven
        C_PerkList *m_pPerkList;                  // 0x08
        std::uint16_t m_statPoints[kStatBanks];   // 0x10
        std::uint16_t m_skillPoints[kSkillBanks]; // 0x24
        std::uint16_t m_mainPoints;               // 0x6A
        std::byte m_padding006C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_PerkLearningState, m_pPerkList) == 0x08, "C_PerkLearningState::m_pPerkList offset mismatch");
            static_assert(offsetof(C_PerkLearningState, m_statPoints) == 0x10, "C_PerkLearningState::m_statPoints offset mismatch");
            static_assert(offsetof(C_PerkLearningState, m_skillPoints) == 0x24, "C_PerkLearningState::m_skillPoints offset mismatch");
            static_assert(offsetof(C_PerkLearningState, m_mainPoints) == 0x6A, "C_PerkLearningState::m_mainPoints offset mismatch");
        }
    };

    static_assert(sizeof(C_PerkLearningState) == 0x70, "C_PerkLearningState size mismatch");
} // namespace SDK::wh::rpgmodule
