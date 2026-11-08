#pragma once

#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_combat_pricing.h"
#include "shared/game/combat_catalog.h"

#include <mafianet/bit_stream.h>

#include <algorithm>
#include <cmath>

namespace KCDC::Tests::CombatPricing {
    inline bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-3F;
    }

    /** A sword of attack 100 that splits it evenly across the passes, needing the strength the attacker has. */
    inline KCDC::Shared::Game::CombatWeaponEntry Sword() {
        return {{1, 1}, 100.0F, {1.0F, 1.0F, 1.0F}, 50.0F, 10, 10, 1, -1};
    }

    /** A long chain shirt: archetype 19, a quarter of the body short of half, covering the upper front. */
    inline KCDC::Shared::Game::CombatArmorEntry ChainShirt() {
        return {{2, 2}, {30.0F, 40.0F, 20.0F}, 19};
    }

    /** A slash at the upper front body, full energy, full stamina. */
    inline KCDC::Shared::Game::CombatPricingBlow Slash() {
        KCDC::Shared::Game::CombatPricingBlow blow;
        blow.attackTypeId = 1;
        blow.hitSubpart   = 1;
        return blow;
    }

    inline KCDC::Shared::Game::CombatPricingVictim Fresh() {
        KCDC::Shared::Game::CombatPricingVictim victim;
        victim.stamina        = 100.0F;
        victim.staminaMaximum = 100.0F;
        return victim;
    }

    /** A soul with one `wat*` term and nothing else. */
    class WeaponAttackScale final: public KCDC::Shared::Game::CombatModifierSource {
      public:
        explicit WeaponAttackScale(float scale): _scale(scale) {}

        float Apply(KCDC::Shared::Game::CombatSide side, KCDC::Shared::Game::CombatModifier modifier, float value) const override {
            return side == KCDC::Shared::Game::CombatSide::Attacker && modifier == KCDC::Shared::Game::CombatModifier::WeaponAttack ? (_scale - 1.0F) * value + value : value;
        }

      private:
        float _scale;
    };
} // namespace KCDC::Tests::CombatPricing

MODULE(combat_pricing, {
    IT("reproduces the engine's price primitives", {
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatBias(24.0F / 42.0F, 0.25F), 0.3076923F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatBias(0.5F, 0.0F), 0.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatConditionFactor(1.0F, 0.6F), 1.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatConditionFactor(-1.0F, 0.6F), 1.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatConditionFactor(0.25F, 0.6F), 0.7F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatStatToAttackMultiplier(10.0F, 10.0F), 1.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatStatToAttackMultiplier(25.0F, 10.0F), 1.1F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatStatToAttackMultiplier(0.0F, 10.0F), 0.5F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatRound2(0.704F), 0.7F), true);
    });

    IT("carries the tables the price reads", {
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::g_CombatHumanBodyImportance, 42.0F), true);
        EQUALS(KCDC::Shared::Game::g_CombatHeroArchetype.id, 13);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::g_CombatHeroArchetype.bodyBaseArmor, 12.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(KCDC::Shared::Game::CombatArchetypeCoverageOf(19), 24.0F), true);
        EQUALS(KCDC::Shared::Game::CombatArchetypeCovers(19, 1), true);
        EQUALS(KCDC::Shared::Game::CombatDamageTypesOf(1, 1, -1), 6);
        EQUALS(KCDC::Shared::Game::CombatDamageTypesOf(0, 5, 1), 4);
        EQUALS(KCDC::Shared::Game::FindCombatAttackType(5)->cannotKill, true);
        EQUALS(KCDC::Shared::Game::FindCombatSubpart(23)->head, true);
        EQUALS(std::is_sorted(KCDC::Shared::Game::g_CombatWeapons.begin(), KCDC::Shared::Game::g_CombatWeapons.end(),
                   [](const auto &a, const auto &b) {
                       return a.itemClass < b.itemClass;
                   }),
            true);
        // dfea5d01-... is `arrow_crude`, a missile: not a melee weapon.
        EQUALS(KCDC::Shared::Game::FindCombatWeapon(KCDC::Shared::Game::ItemClassIdFromGuid("dfea5d01-b25c-414a-9ab4-6911a5f82118")) == nullptr, true);
    });

    IT("prices a punch on a bare body", {
        KCDC::Shared::Game::CombatPricingBlow blow = KCDC::Tests::CombatPricing::Slash();
        blow.attackTypeId                          = 5;
        blow.hitSubpart                            = -1;
        KCDC::Shared::Game::CombatPricingAttacker attacker;
        attacker.strength                           = 10.0F;
        const KCDC::Shared::Game::CombatPrice price = KCDC::Shared::Game::PriceMeleeBlow(blow, attacker, KCDC::Tests::CombatPricing::Fresh(), KCDC::Shared::Game::CombatModifierSource {});
        // 25 base x 1.08 for strength over the punch's requirement of 2; the
        // bare body's 12 against it in the one smash pass.
        EQUALS(KCDC::Tests::CombatPricing::Near(price.attack[2], 27.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.armor[2], 12.0F), true);
        EQUALS(price.damageTypes, 4);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.health, 5.25F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.stamina, 27.5F), true);
        EQUALS(price.mayKill, false);
    });

    IT("prices a slash through a covering chain shirt", {
        const KCDC::Shared::Game::CombatWeaponEntry sword = KCDC::Tests::CombatPricing::Sword();
        const KCDC::Shared::Game::CombatArmorEntry shirt  = KCDC::Tests::CombatPricing::ChainShirt();
        KCDC::Shared::Game::CombatPricingAttacker attacker;
        attacker.weapon                                = &sword;
        attacker.strength                              = 10.0F;
        KCDC::Shared::Game::CombatPricingVictim victim = KCDC::Tests::CombatPricing::Fresh();
        victim.armor.push_back({&shirt, -1.0F});

        const KCDC::Shared::Game::CombatPrice price = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        // The shirt covers the subpart, so its whole defence counts there.
        EQUALS(price.damageTypes, 6);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.armor[1], 52.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.armor[2], 32.0F), true);
        // Slash 48 over, smash 68 over: (2.4 + 3.4) x 7; stamina 33.8 + 37.7 capped at 40.
        EQUALS(KCDC::Tests::CombatPricing::Near(price.health, 40.6F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(price.stamina, 40.0F), true);
        EQUALS(price.mayKill, true);

        // Off the covered subpart only the weighted share protects.
        KCDC::Shared::Game::CombatPricingBlow leg = KCDC::Tests::CombatPricing::Slash();
        leg.hitSubpart                            = 200;
        const KCDC::Shared::Game::CombatPrice off = KCDC::Shared::Game::PriceMeleeBlow(leg, attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        EQUALS(KCDC::Tests::CombatPricing::Near(off.armor[1], 0.3076923F * 40.0F + 12.0F), true);
    });

    IT("takes nothing for a perfect block and the minimum stamina for a held one", {
        const KCDC::Shared::Game::CombatWeaponEntry sword = KCDC::Tests::CombatPricing::Sword();
        KCDC::Shared::Game::CombatPricingAttacker attacker;
        attacker.weapon                                = &sword;
        attacker.strength                              = 10.0F;
        KCDC::Shared::Game::CombatPricingVictim victim = KCDC::Tests::CombatPricing::Fresh();
        victim.blockWeapon                             = &sword;

        KCDC::Shared::Game::CombatPricingBlow blow    = KCDC::Tests::CombatPricing::Slash();
        blow.blocked                                  = true;
        blow.perfectBlock                             = true;
        const KCDC::Shared::Game::CombatPrice perfect = KCDC::Shared::Game::PriceMeleeBlow(blow, attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        EQUALS(KCDC::Tests::CombatPricing::Near(perfect.health, 0.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(perfect.stamina, 0.0F), true);

        // A block of 50 against an attack of 100 is overrun: a third of it holds.
        blow.perfectBlock                          = false;
        const KCDC::Shared::Game::CombatPrice held = KCDC::Shared::Game::PriceMeleeBlow(blow, attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        EQUALS(KCDC::Tests::CombatPricing::Near(held.block, 50.0F), true);
        EQUALS(held.health > 0.0F, true);
        EQUALS(held.stamina >= 15.0F, true);

        // A stronger guard than the swing holds it whole: no health, and the
        // least stamina each of the two passes can cost.
        victim.defenseSkill                          = 30.0F;
        KCDC::Shared::Game::CombatWeaponEntry shield = sword;
        shield.defense                               = 250.0F;
        shield.weaponClass                           = KCDC::Shared::Game::kShieldWeaponClass;
        victim.blockWeapon                           = &shield;
        const KCDC::Shared::Game::CombatPrice wall   = KCDC::Shared::Game::PriceMeleeBlow(blow, attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        EQUALS(KCDC::Tests::CombatPricing::Near(wall.health, 0.0F), true);
        EQUALS(KCDC::Tests::CombatPricing::Near(wall.stamina, 30.0F), true);
    });

    IT("scales through the soul's modifiers and the gamemode's", {
        const KCDC::Shared::Game::CombatWeaponEntry sword = KCDC::Tests::CombatPricing::Sword();
        KCDC::Shared::Game::CombatPricingAttacker attacker;
        attacker.weapon                                      = &sword;
        attacker.strength                                    = 10.0F;
        const KCDC::Shared::Game::CombatPricingVictim victim = KCDC::Tests::CombatPricing::Fresh();
        const KCDC::Shared::Game::CombatPrice plain          = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        const KCDC::Shared::Game::CombatPrice buffed         = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Tests::CombatPricing::WeaponAttackScale(1.2F));
        EQUALS(KCDC::Tests::CombatPricing::Near(buffed.attack[1], 120.0F), true);
        EQUALS(buffed.health > plain.health, true);

        KCDC::Shared::Game::CombatPricingScales scales;
        scales.health                              = 0.5F;
        const KCDC::Shared::Game::CombatPrice half = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Shared::Game::CombatModifierSource {}, scales);
        EQUALS(KCDC::Tests::CombatPricing::Near(half.health, plain.health * 0.5F), true);

        scales                                       = {};
        scales.defense                               = 0.0F;
        scales.attack                                = 2.0F;
        const KCDC::Shared::Game::CombatPrice brutal = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Shared::Game::CombatModifierSource {}, scales);
        EQUALS(KCDC::Tests::CombatPricing::Near(brutal.armor[1], 0.0F), true);
        // 200 through nothing in each pass: (10 + 10) x 7.
        EQUALS(KCDC::Tests::CombatPricing::Near(brutal.health, 140.0F), true);
    });

    IT("prices a riposte's strike-back charged", {
        const KCDC::Shared::Game::CombatWeaponEntry sword = KCDC::Tests::CombatPricing::Sword();
        KCDC::Shared::Game::CombatPricingAttacker attacker;
        attacker.weapon                                      = &sword;
        attacker.strength                                    = 10.0F;
        const KCDC::Shared::Game::CombatPricingVictim victim = KCDC::Tests::CombatPricing::Fresh();
        const KCDC::Shared::Game::CombatPrice plain          = KCDC::Shared::Game::PriceMeleeBlow(KCDC::Tests::CombatPricing::Slash(), attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        KCDC::Shared::Game::CombatPricingBlow riposte        = KCDC::Tests::CombatPricing::Slash();
        riposte.attackCharge                                 = 1.0F;
        const KCDC::Shared::Game::CombatPrice charged        = KCDC::Shared::Game::PriceMeleeBlow(riposte, attacker, victim, KCDC::Shared::Game::CombatModifierSource {});
        // 1 + AttackCharge x AttackChargingAttMod (0.1).
        EQUALS(KCDC::Tests::CombatPricing::Near(charged.attack[1], plain.attack[1] * 1.1F), true);
        EQUALS(charged.health > plain.health, true);
    });

    IT("round trips a report's pricing inputs and refuses them on an arrow", {
        KCDC::Shared::RPC::CombatHitReport report;
        report.incarnation               = 2;
        report.attackerNetworkId         = 7;
        report.attackGeneration          = 31;
        report.hasInputs                 = true;
        report.inputs.damageTypes        = 6;
        report.inputs.staminaCoefficient = 0.8F;
        report.inputs.energy             = 0.9F;
        report.inputs.relativeSpeed      = 4.5F;
        report.inputs.angle              = 2.8F;
        report.inputs.mounted            = true;
        report.inputs.attackCharged      = true;
        report.inputs.victimStamina      = 64.0F;
        EQUALS(report.IsSane(), true);

        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatHitReport decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.hasInputs, true);
        EQUALS(decoded.inputs.damageTypes, 6);
        EQUALS(decoded.inputs.mounted, true);
        EQUALS(decoded.inputs.attackCharged, true);
        EQUALS(KCDC::Tests::CombatPricing::Near(decoded.inputs.victimStamina, 64.0F), true);

        report.inputs.energy = 1.5F;
        EQUALS(report.IsSane(), false);
        report.inputs.energy = 1.0F;
        report.source        = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitSource::Missile);
        report.shotSequence  = 4;
        EQUALS(report.IsSane(), false);
    });
});
