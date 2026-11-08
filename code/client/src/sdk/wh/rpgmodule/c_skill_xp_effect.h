#pragma once

#include "../framework/wuid.h"
#include "c_event.h"
#include "c_soul_effect.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * One native skill XP gain.
     *
     * Every source -- a hit, a lockpick, reading, a quest reward, a teacher,
     * `AdvanceToSkillLevel` -- builds one of these on an RPG event, and
     * `C_EventManager::ProcessEvent` applies it: `Apply` (0x180C65588, slot 3)
     * scales the request by `XPMultiplier` (derived stat 47) and the
     * per-skill multiplier when `m_applyMultipliers` is set, then writes the
     * soul's skill table at `C_Soul+0x5E8`. Constructor 0x180C65AD0,
     * allocated 0x40 by every creator below.
     *
     * XP is Q7 fixed point throughout: whole XP times 128.
     */
    class C_SkillXPEffect: public C_SoulEffect {
      public:
        std::uint32_t GetSkill() const {
            return m_skill;
        }

        /** What was asked for, before any multiplier. */
        std::uint32_t GetRequestedXpQ7() const {
            return m_requestedXpQ7;
        }

        bool AppliesMultipliers() const {
            return m_applyMultipliers;
        }

        /** Levels the apply crossed. Read after `Apply`; the HUD lambda at 0x181FBF300 keys its level-up popup on it. */
        std::uint32_t GetLevelsGained() const {
            return m_levelsGained;
        }

        /**
         * A gain the caller refuses, left as a well-formed no-op: nothing
         * applied and nothing gained, which is exactly what the HUD lambda
         * reads as "show nothing" and what the perk-point listeners read as
         * "grant nothing".
         */
        void Withhold() {
            m_appliedXpQ7  = 0;
            m_levelsGained = 0;
        }

        std::uint32_t GetInducedSkill() const {
            return m_inducedSkill;
        }
        /** Finalize an exact positive assignment before deferred event dispatch. */
        void RecordExactGain(std::uint32_t xpQ7, std::uint32_t level, std::uint32_t inducedLevel) {
            m_requestedXpQ7 = m_appliedXpQ7 = xpQ7;
            m_levelsGained                  = level > m_levelBefore ? level - m_levelBefore : 0;
            m_inducedLevelsGained           = m_inducedSkill < 35 && inducedLevel > m_inducedLevelBefore ? inducedLevel - m_inducedLevelBefore : 0;
        }

        /**
         * Builds a gain on `event`. `scaled` picks the creator the game's own
         * gameplay uses (`CreateSkillXPEffect180C65A20`, multipliers on) or
         * the one level advances use (`CreateSkillXPEffectUnscaled1816849B4`,
         * multipliers off). Both take an unused first argument -- the event
         * travels in RDX -- and attach the effect to the event themselves.
         */
        static C_SkillXPEffect *Create(C_Event *event, framework::WUID soul, std::uint32_t skill, std::uint32_t xpQ7, bool scaled) {
            return hook::call<C_SkillXPEffect *>(scaled ? gPatterns.rpgmodule__CreateSkillXPEffect : gPatterns.rpgmodule__CreateSkillXPEffectUnscaled, static_cast<void *>(nullptr), event, soul, skill, &xpQ7);
        }

      private:
        std::uint32_t m_skill;               // 0x18, E_SoulSkill
        std::uint32_t m_requestedXpQ7;       // 0x1C
        std::uint32_t m_appliedXpQ7;         // 0x20
        std::uint32_t m_levelBefore;         // 0x24
        std::uint32_t m_levelsGained;        // 0x28
        std::uint32_t m_inducedSkill;        // 0x2C, 35 for none; the weapon skills induce fencing
        std::uint32_t m_inducedLevelBefore;  // 0x30
        std::uint32_t m_inducedLevelsGained; // 0x34
        bool m_applyMultipliers;             // 0x38
        std::byte m_padding0039[0x3]; // ctor 0x180C65AD0 writes nothing here
        std::uint32_t m_queryReason; // 0x3C, wh::rpgmodule::QueryReason

        static void AssertLayout() {
            static_assert(offsetof(C_SkillXPEffect, m_levelBefore) == 0x24, "C_SkillXPEffect::m_levelBefore offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_inducedLevelBefore) == 0x30, "C_SkillXPEffect::m_inducedLevelBefore offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_inducedLevelsGained) == 0x34, "C_SkillXPEffect::m_inducedLevelsGained offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_skill) == 0x18, "C_SkillXPEffect::m_skill offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_requestedXpQ7) == 0x1C, "C_SkillXPEffect::m_requestedXpQ7 offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_appliedXpQ7) == 0x20, "C_SkillXPEffect::m_appliedXpQ7 offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_levelsGained) == 0x28, "C_SkillXPEffect::m_levelsGained offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_inducedSkill) == 0x2C, "C_SkillXPEffect::m_inducedSkill offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_applyMultipliers) == 0x38, "C_SkillXPEffect::m_applyMultipliers offset mismatch");
            static_assert(offsetof(C_SkillXPEffect, m_queryReason) == 0x3C, "C_SkillXPEffect::m_queryReason offset mismatch");
        }
    };

    static_assert(sizeof(C_SkillXPEffect) == 0x40, "C_SkillXPEffect size mismatch");
} // namespace SDK::wh::rpgmodule
