#pragma once

#include "../entitymodule/c_actor.h"
#include "c_combat_actor.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * The local player's opponent candidates, which the combat player
     * controller points at from +0x20. The engine's own name for the type is stripped;
     * this one says what it holds.
     *
     * `UpdateOpponentCandidates` (0x1808315BC) refills it every frame --
     * from the AI grid through `OnGridOpponentCandidate` (0x181E571B0), or
     * from every actor when `wh_cs_OpponentManagerBruteForce` is set -- and
     * `FindAttackTarget` (0x180686A50) picks the target out of it. A body only
     * gets in through the per-candidate refusal `ShouldSkipOpponentCandidate`
     * (0x1809DFE38), whose first test skips anything IsPlayerControlled.
     */
    class C_CombatOpponentCandidates {
      public:
        C_CombatActor *GetOwner() const {
            return m_pOwner;
        }

        bool Contains(const C_CombatActor *fighter) const {
            for (C_CombatActor *const *candidate = m_candidatesBegin; candidate != nullptr && candidate != m_candidatesEnd; ++candidate) {
                if (*candidate == fighter) {
                    return true;
                }
            }
            return false;
        }

        /**
         * Runs `actor` through the grid path's own per-candidate callback,
         * which applies every native test before it pushes the actor's
         * fighter: the owner's CanFightActor, ShouldSkipOpponentCandidate, the
         * weapon-range and frontal bookkeeping. The callback is a lambda that
         * captured the list by reference, hence the extra indirection.
         */
        void Offer(entitymodule::C_Actor *actor) {
            C_CombatOpponentCandidates *self = this;
            hook::call<void>(gPatterns.CombatOpponentCandidates__OnCandidate, &self, actor);
        }

      private:
        C_CombatActor *m_pOwner;              // 0x00
        C_CombatActor **m_candidatesBegin;    // 0x08
        C_CombatActor **m_candidatesEnd;      // 0x10
        C_CombatActor **m_candidatesCapacity; // 0x18
        std::byte m_frontal0020[0x10];        // 0x20, std::list of the frontal candidates
        std::uint32_t m_inRangeEntityId0030;  // 0x30

        static void AssertLayout() {
            static_assert(offsetof(C_CombatOpponentCandidates, m_candidatesBegin) == 0x08, "C_CombatOpponentCandidates::m_candidatesBegin offset mismatch");
            static_assert(offsetof(C_CombatOpponentCandidates, m_frontal0020) == 0x20, "C_CombatOpponentCandidates::m_frontal0020 offset mismatch");
            static_assert(offsetof(C_CombatOpponentCandidates, m_inRangeEntityId0030) == 0x30, "C_CombatOpponentCandidates::m_inRangeEntityId0030 offset mismatch");
        }
    };
} // namespace SDK::wh::combatmodule
