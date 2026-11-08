#pragma once

#include "features/player/player_combat_modifiers.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_combat_pricing.h"
#include "shared/features/player/player_entity.h"
#include "shared/item_stack.h"

#include <optional>
#include <string_view>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * One melee blow between two players as the server prices it: the hit
     * data the resolving engine reported, and everything else from what the
     * server holds -- the weapon drawn in the attacker's hand, the armour the
     * victim wears and its wear, the victim's stamina, both souls' strength
     * and skills, and their buffs and perks (`CombatSoulTerms`).
     */
    class CombatBlow final {
      public:
        /**
         * Gathers a melee report's blow; nothing for one without hit data, or
         * for anything but a swing. `counter` when the swing is one the server
         * opened as a riposte's strike-back: only then does the charge the
         * report names count.
         */
        static std::optional<CombatBlow> From(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim, bool counter);

        KCDC::Shared::Game::CombatPrice Price(const KCDC::Shared::Game::CombatPricingScales &scales = {}) const;

        /** The weapon the blow was priced with, or null for a bare hand. */
        const KCDC::Shared::Game::CombatWeaponEntry *Weapon() const {
            return _attacker.weapon;
        }

      private:
        /** An effective skill of a player, by its `E_SoulSkill` name: the replicated one where it travels, else the accepted level. */
        static float Skill(KCDC::Shared::Entities::HumanEntity &player, std::string_view name);

        /**
         * The best health of any row of each of `itemClasses` the player
         * carries, -1 for one never worn down or not carried: one pass over
         * the inventory for the whole blow.
         */
        static std::vector<float> ItemHealths(std::uint64_t playerNetworkId, const std::vector<KCDC::Shared::RPC::ItemClassId> &itemClasses);

        KCDC::Shared::Game::CombatPricingBlow _blow;
        KCDC::Shared::Game::CombatPricingAttacker _attacker;
        KCDC::Shared::Game::CombatPricingVictim _victim;
        CombatSoulTerms _attackerTerms;
        CombatSoulTerms _victimTerms;
    };
} // namespace KCDC::Features::Player
