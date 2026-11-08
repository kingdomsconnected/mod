#pragma once

#include "../../patterns.h"

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * `combat_input_class` row ids, read where the database keeps them. Only
     * the ones the mod presses are named.
     */
    class C_CombatInputClassDatabase final {
      public:
        /**
         * `attack_heavy`'s id (database +0xF4): despite the name, what the
         * left click attacks with. -1 until the database has loaded, so read
         * it at the moment of use, as the game does.
         */
        static std::int32_t GetAttackHeavyId() {
            return *reinterpret_cast<const std::int32_t *>(gPatterns.CombatInputClassId__AttackHeavy);
        }
    };
} // namespace SDK::wh::combatmodule
