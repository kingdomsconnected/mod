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
     * One stat XP gain: the stat counterpart of `C_SkillXPEffect`, and the
     * native earning path. `Apply` (0x180C653E4, slot 3) always scales by
     * the per-stat `XPGainMultiplier` (derived stats 172-176), and also by
     * `XPMultiplier` when `m_applyMultipliers` is set. It writes the stat
     * table at `C_Soul+0x56C`, and records the main
     * level (derived stat 28) on either side. Constructor 0x180C67268, allocated 0x40.
     */
    class C_StatXPEffect: public C_SoulEffect {
      public:
        std::uint32_t GetStat() const {
            return m_stat;
        }

        std::uint32_t GetRequestedXpQ7() const {
            return m_requestedXpQ7;
        }

        bool AppliesMultipliers() const {
            return m_applyMultipliers;
        }

        std::uint32_t GetLevelsGained() const {
            return m_levelsGained;
        }

        /** See `C_SkillXPEffect::Withhold`. */
        void Withhold() {
            m_appliedXpQ7      = 0;
            m_levelsGained     = 0;
            m_mainLevelsGained = 0;
        }

        /** Finalize an exact positive assignment before deferred event dispatch. */
        void RecordExactGain(std::uint32_t xpQ7, std::uint32_t level, std::uint32_t mainLevel) {
            m_requestedXpQ7 = m_appliedXpQ7 = xpQ7;
            m_levelsGained                  = level > m_levelBefore ? level - m_levelBefore : 0;
            m_mainLevelsGained              = mainLevel > m_mainLevelBefore ? mainLevel - m_mainLevelBefore : 0;
        }

        /** `CreateStatXPEffect180C66EF0` or `CreateStatXPEffectUnscaled180FA3218`; same shape as the skill creators. */
        static C_StatXPEffect *Create(C_Event *event, framework::WUID soul, std::uint32_t stat, std::uint32_t xpQ7, bool scaled) {
            return hook::call<C_StatXPEffect *>(scaled ? gPatterns.rpgmodule__CreateStatXPEffect : gPatterns.rpgmodule__CreateStatXPEffectUnscaled, static_cast<void *>(nullptr), event, soul, stat, &xpQ7);
        }

      private:
        std::uint32_t m_stat;             // 0x18, E_SoulStat
        std::uint32_t m_requestedXpQ7;    // 0x1C
        std::uint32_t m_appliedXpQ7;      // 0x20
        std::uint32_t m_levelBefore;      // 0x24
        std::uint32_t m_levelsGained;     // 0x28
        std::uint32_t m_mainLevelBefore;  // 0x2C
        std::uint32_t m_mainLevelsGained; // 0x30
        bool m_applyMultipliers;          // 0x34
        std::byte m_padding0035[0x3]; // ctor 0x180C67268 writes nothing here
        std::uint32_t m_queryReason; // 0x38
        std::byte m_padding003C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_StatXPEffect, m_levelBefore) == 0x24, "C_StatXPEffect::m_levelBefore offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_mainLevelBefore) == 0x2C, "C_StatXPEffect::m_mainLevelBefore offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_stat) == 0x18, "C_StatXPEffect::m_stat offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_requestedXpQ7) == 0x1C, "C_StatXPEffect::m_requestedXpQ7 offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_appliedXpQ7) == 0x20, "C_StatXPEffect::m_appliedXpQ7 offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_levelsGained) == 0x28, "C_StatXPEffect::m_levelsGained offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_mainLevelsGained) == 0x30, "C_StatXPEffect::m_mainLevelsGained offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_applyMultipliers) == 0x34, "C_StatXPEffect::m_applyMultipliers offset mismatch");
            static_assert(offsetof(C_StatXPEffect, m_queryReason) == 0x38, "C_StatXPEffect::m_queryReason offset mismatch");
        }
    };

    static_assert(sizeof(C_StatXPEffect) == 0x40, "C_StatXPEffect size mismatch");
} // namespace SDK::wh::rpgmodule
