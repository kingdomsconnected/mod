#pragma once

#include "shared/features/player/player_combat_pricing.h"
#include "shared/game/buff_rule_catalog.h"
#include "shared/game/soul_scalar.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * The buff parameters one player's soul feeds a blow through: the perks
     * of their accepted progression and the buffs their client last
     * reported, each through the buff's authored `buff_params`. The perks
     * are the server's own record; the reported buffs are the client's word,
     * which the server does not check against anything.
     *
     * A buff whose implementation applies its parameters always
     * (`Cpp:Constant`) or for as long as it runs (`Cpp:BasicTimed`, which
     * the report says is running) counts. So does `Cpp:Mounted` on a blow
     * struck from the saddle. `Cpp:VersusAnimal` never applies between two
     * players. Every other implementation gates its parameters on a
     * condition of the fight -- a heavy weapon, the first strike, a chain of
     * blows -- the server does not follow, and is left out; `Skipped` counts
     * those that named a parameter a blow reads.
     */
    class CombatSoulTerms final {
      public:
        /** Collects `playerNetworkId`'s terms; none for a player the server has no progression for. */
        void Collect(std::uint64_t playerNetworkId, bool mounted);

        /** `value` through every term on `modifier`, in the soul's modifier order. */
        float Apply(KCDC::Shared::Game::CombatModifier modifier, float value) const;

        /** `BodyBaseArmor` (derived stat 89, `bba`) from the archetype's base. */
        float BodyBaseArmor(float base) const;

        /** Whether one of the soul's accepted perks grants `ability` (`perk_soul_ability`). */
        bool HasAbility(std::uint32_t ability) const {
            return std::find(_abilities.begin(), _abilities.end(), ability) != _abilities.end();
        }

        /** Sources the server could not evaluate that named a parameter a blow reads. */
        std::size_t Skipped() const {
            return _skipped;
        }

        /** The `buff_params` token of a modifier. */
        static std::string_view Token(KCDC::Shared::Game::CombatModifier modifier);

      private:
        struct Term {
            std::string_view token;
            KCDC::Shared::Game::SoulModifierOperation operation;
            float value;
        };

        /** One buff's terms a blow reads, as its authored `buff_params` give them. */
        struct BuffTerms {
            std::vector<Term> terms;

            /** Whether a `;` splits the parameters into on and off states, which only the implementation chooses between. */
            bool toggled = false;

            std::string_view implementation;
        };

        /** The perks that grant a buff or an ability, by catalog index, and every buff a perk grants. */
        struct PerkBuffs {
            std::vector<std::pair<std::size_t, std::string_view>> grants;
            std::vector<std::pair<std::size_t, std::uint32_t>> abilities;
            std::unordered_set<std::string_view> buffs;
        };

        /** Every authored buff's terms, parsed once from the catalog. */
        static const std::unordered_map<std::string_view, BuffTerms> &ParsedBuffs();

        /** The perk-to-buff edges, resolved once from the catalog. */
        static const PerkBuffs &PerkBuffSources();

        /** The terms one buff rule's parameters name. */
        static BuffTerms Parse(const KCDC::Shared::Game::BuffRuleDefinition &rule);

        /** Adds the terms of one buff, or counts it as skipped. */
        void AddBuff(std::string_view buffGuid, bool mounted);

        float ApplyToken(std::string_view token, float value) const;

        std::vector<Term> _terms;
        std::vector<std::uint32_t> _abilities;
        std::size_t _skipped = 0;
    };

    /** The two souls of one blow, answering the pricing's `ApplyModifiers` calls. */
    class CombatPricingModifiers final: public KCDC::Shared::Game::CombatModifierSource {
      public:
        CombatPricingModifiers(const CombatSoulTerms &attacker, const CombatSoulTerms &victim): _attacker(attacker), _victim(victim) {}

        float Apply(KCDC::Shared::Game::CombatSide side, KCDC::Shared::Game::CombatModifier modifier, float value) const override {
            return (side == KCDC::Shared::Game::CombatSide::Attacker ? _attacker : _victim).Apply(modifier, value);
        }

        float BodyBaseArmor(float base) const override {
            return _victim.BodyBaseArmor(base);
        }

        bool HasAbility(std::uint32_t ability) const override {
            return _attacker.HasAbility(ability);
        }

      private:
        const CombatSoulTerms &_attacker;
        const CombatSoulTerms &_victim;
    };
} // namespace KCDC::Features::Player
