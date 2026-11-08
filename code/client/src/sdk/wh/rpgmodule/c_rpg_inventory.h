#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "c_soul.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    /**
     * The RPG rules for items: prices, loot, consumption, repair.
     *
     * A stateless object -- the global at 0x18492EC88 is nothing but its vptr
     * (table 0x1840AEDA8, 49 slots) -- that the game passes as `this` to every
     * rule it asks about. The mod meets it as the `this` of a detour, and
     * calls the one rule it needs through its body rather than the table, so
     * no slot is declared. Proven so far: slot 6, `GetShopItemPrice`
     * (0x180566410); slot 23, `ApplyRepairKit` (0x182CFE688); slot 24,
     * `ApplyShoppingTransaction` (0x180785AA4); slot 37,
     * `CalculateItemDecay` (0x181284168, food and herbs only); slot 38,
     * `ApplyInventoryPerk` (0x1811A0A64).
     */
    class C_RPGInventory final {
      public:
        /** The global the game's own callers pass as `this`. */
        static C_RPGInventory *GetInstance() {
            return reinterpret_cast<C_RPGInventory *>(gPatterns.C_RPGInventory__Instance);
        }

        /**
         * Slot 38: how the perk screen learns a perk. Builds an RPG event with
         * a `HUD/INVENTORY` cause on `soul` and attaches a
         * `C_LearnPerkEffect` (`learn`) or a `C_AddPerkEffect`, then processes
         * it. The learn effect spends the point through
         * `C_PerkLearningState::LearnPerk`, and the event's dispatch is what
         * refreshes the perk screen and plays its "perk learned" sound -- or
         * opens `ui_LearnedFailed` when the learn was refused.
         */
        void ApplyInventoryPerk(I_Soul *soul, const CryGUID &perk, bool learn) {
            hook::this_call<void>(gPatterns.C_RPGInventory__ApplyInventoryPerk, this, soul, &perk, learn);
        }
    };
} // namespace SDK::wh::rpgmodule
