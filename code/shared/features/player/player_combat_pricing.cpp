#include "shared/features/player/player_combat_pricing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Shared::Game {
    namespace {
        struct PricingSteps final {
            const CombatModifierSource &modifiers;

            /** One `ApplyModifiers`, clamped at zero as every id a blow reads is. */
            float Apply(CombatSide side, CombatModifier modifier, float value) const {
                return std::max(modifiers.Apply(side, modifier, value), 0.0F);
            }
        };

        /**
         * `RemapRangeClamped` (0x180466E28): `x` clamped into the first range,
         * then `RemapRange` (0x180466E88), which measures from the far end of
         * an ascending range and back from the top of an ascending target.
         * Equal to the textbook remap, but not bit for bit.
         */
        float RemapRangeClamped(float x, float a, float b, float c, float d) {
            const float lo = std::min(a, b);
            const float hi = std::max(a, b);
            x              = std::min(std::max(x, lo), hi);
            const float cl = std::min(c, d);
            const float ch = std::max(c, d);
            const float t  = lo == a ? ((hi - x) * (ch - cl)) / (hi - lo) : ((x - lo) * (ch - cl)) / (hi - lo);
            return cl == c ? ch - t : cl + t;
        }

        /** Half away from zero, as `(int)` of a value nudged by a half. */
        float RoundHalfAway(float value) {
            return static_cast<float>(static_cast<std::int32_t>(value + (value < 0.0F ? -0.5F : 0.5F)));
        }

        /** The stamina a blow costs per pass (`StamDmg`), before the per-pass modifiers. */
        float StaminaDamage(float dmgR, bool weak, bool perfect) {
            const float base = RemapRangeClamped(dmgR, CombatPricingParams::kMinStaminaDmgR, CombatPricingParams::kMaxStaminaDmgR, CombatPricingParams::kMinStaminaDamage, CombatPricingParams::kMaxStaminaDamage);
            return base * (weak ? CombatPricingParams::kWeakBlockStamCoef : 1.0F) * (perfect ? 0.0F : 1.0F);
        }

        /** `CalculateHealthDamage` (0x180C66684), before the subpart coefficient. */
        float HealthDamage(float dmgR, float stamina, float staminaLoss) {
            float health = 0.0F;
            if (staminaLoss - stamina > 0.0F) {
                health = (staminaLoss - stamina) * (dmgR <= 0.0F ? CombatPricingParams::kStaminaDeficitHealthDamageB : CombatPricingParams::kStaminaDeficitHealthDamageA) + 0.0F;
            }
            if (dmgR > 0.0F) {
                health = health + dmgR * CombatPricingParams::kDmgRToHealthCoefB;
            }
            return health;
        }

        /** The attack vector of a weapon (0x180686FDC -> 0x180687704). */
        std::array<float, 3> WeaponAttack(const PricingSteps &steps, const CombatPricingAttacker &attacker) {
            const CombatWeaponEntry &weapon = *attacker.weapon;
            const auto requirement          = static_cast<std::int32_t>(steps.Apply(CombatSide::Attacker, CombatModifier::WeaponStrengthReq, static_cast<float>(weapon.strengthRequirement)));
            const float strength            = steps.Apply(CombatSide::Attacker, CombatModifier::WeaponStrength, attacker.strength);
            const float condition           = CombatConditionFactor(attacker.weaponHealth, CombatPricingParams::kWeaponStatusToAttackCoef);
            float attack                    = condition * weapon.attack;
            attack                          = CombatStatToAttackMultiplier(strength, static_cast<float>(requirement)) * attack;
            float swing                     = (attacker.skill + CombatPricingParams::kSkillToDmgConstA) * attack / CombatPricingParams::kSkillToDmgConstA;
            swing                           = steps.Apply(CombatSide::Attacker, CombatModifier::WeaponAttack, swing);
            return {steps.Apply(CombatSide::Attacker, CombatModifier::StabAttack, swing * weapon.attackMods[0]), steps.Apply(CombatSide::Attacker, CombatModifier::SlashAttack, swing * weapon.attackMods[1]),
                steps.Apply(CombatSide::Attacker, CombatModifier::SmashAttack, swing * weapon.attackMods[2])};
        }

        /** The attack vector of a bare hand (0x1806875BC). */
        std::array<float, 3> UnarmedAttack(const PricingSteps &steps, const CombatPricingAttacker &attacker, std::int32_t attackTypeId) {
            const CombatAttackTypeEntry *row = FindCombatAttackType(attackTypeId);
            const float cost                 = row != nullptr ? row->relativeStaminaCost : 1.0F;
            const float base                 = cost * attacker.archetype->unarmedAttackBase;
            const float requirement          = static_cast<float>(static_cast<std::int32_t>(RoundHalfAway(cost * CombatPricingParams::kUnarmedAttackReqStrBase)));
            const float multiplier           = CombatStatToAttackMultiplier(attacker.strength, requirement);
            float swing                      = (attacker.skill + CombatPricingParams::kSkillToDmgConstA) * (multiplier * base) / CombatPricingParams::kSkillToDmgConstA;
            swing                            = steps.Apply(CombatSide::Attacker, CombatModifier::UnarmedAttack, swing);
            return {swing, swing, swing};
        }

        /** The victim's armour at `subpart`, every worn piece weighed by how much of the body it covers (0x180BE16DC -> 0x181FDF500). */
        std::array<float, 3> SubpartArmor(const CombatPricingVictim &victim, std::int32_t subpart) {
            std::array<float, 3> all {};
            std::array<float, 3> cover {};
            for (const CombatWornArmor &worn : victim.armor) {
                if (worn.armor == nullptr) {
                    continue;
                }
                const float condition = CombatConditionFactor(worn.health, CombatPricingParams::kArmorStatusToDefenseCoef);
                const float weight    = CombatBias(CombatArchetypeCoverageOf(worn.armor->archetype) / g_CombatHumanBodyImportance, CombatPricingParams::kAverageArmorDefenseWeight);
                const bool covers     = CombatArchetypeCovers(worn.armor->archetype, subpart);
                for (std::size_t i = 0; i < 3; ++i) {
                    const float defense = condition * worn.armor->defense[i];
                    all[i]              = weight * defense + all[i];
                    if (covers) {
                        cover[i] = (1.0F - weight) * defense + cover[i];
                    }
                }
            }
            return {all[0] + cover[0], all[1] + cover[1], all[2] + cover[2]};
        }

        /** The struck subpart's coefficient (`GetSubpartDamageCoef` 0x180C63C44). */
        float SubpartCoefficient(const PricingSteps &steps, std::int32_t subpart) {
            if (subpart < 0 || g_CombatSubparts.empty() || subpart > g_CombatSubparts.back().id) {
                return 1.0F;
            }
            const CombatSubpartEntry *row = FindCombatSubpart(subpart);
            if (row == nullptr) {
                return 0.0F;
            }
            float coefficient = row->damageCoefficient;
            if (row->head) {
                coefficient = steps.Apply(CombatSide::Attacker, CombatModifier::HeadDamageDealt, coefficient);
                coefficient = steps.Apply(CombatSide::Victim, CombatModifier::HeadDamageTaken, coefficient);
            }
            return coefficient;
        }
    } // namespace

    float CombatRound2(float value) {
        const float scaled = value * 100.0F;
        return static_cast<float>(static_cast<std::int32_t>(scaled + (scaled < 0.0F ? -0.5F : 0.5F))) * 0.0099999998F;
    }

    float CombatConditionFactor(float health01, float coefficient) {
        // An item never worn down carries the creation sentinel.
        const float health = health01 < 0.0F ? 1.0F : health01;
        if (coefficient == 0.0F) {
            return 1.0F;
        }
        return CombatRound2(std::sqrt(health) * coefficient + (1.0F - coefficient));
    }

    float CombatStatToAttackMultiplier(float stat, float requirement) {
        const float difference = std::min(std::max((stat - requirement) / CombatPricingParams::kMaxStatToAttackStatDiff, -1.0F), 1.0F);
        if (difference < 0.0F) {
            return (difference + 1.0F) * (1.0F - CombatPricingParams::kMinStatToAttackMult) + CombatPricingParams::kMinStatToAttackMult;
        }
        return (CombatPricingParams::kMaxStatToAttackMult - 1.0F) * difference + 1.0F;
    }

    float CombatBias(float x, float a) {
        if (a == 0.0F) {
            return 0.0F;
        }
        if (a == 1.0F) {
            return 1.0F;
        }
        return x / ((1.0F / a - 2.0F) * (1.0F - x) + 1.0F);
    }

    CombatPrice PriceMeleeBlow(const CombatPricingBlow &blow, const CombatPricingAttacker &attacker, const CombatPricingVictim &victim, const CombatModifierSource &modifiers, const CombatPricingScales &scales) {
        const PricingSteps steps {modifiers};
        CombatPrice price;
        const CombatAttackTypeEntry *attackType = FindCombatAttackType(blow.attackTypeId);
        price.mayKill                           = attackType == nullptr || !attackType->cannotKill;
        price.damageTypes                       = blow.damageTypes != 0 ? blow.damageTypes : CombatDamageTypesOf(blow.attackTypeId, attacker.weapon != nullptr ? attacker.weapon->weaponClass : -1, attacker.weapon != nullptr ? attacker.weapon->subClass : -1);

        // The block (0x180728170), which the engine works out first.
        float blockDefense = -1.0F;
        bool shield        = false;
        if (blow.blocked || blow.weakBlock) {
            if (victim.blockWeapon == nullptr) {
                blockDefense = CombatPricingParams::kUnarmedBlockDefense;
            }
            else {
                blockDefense = steps.Apply(CombatSide::Victim, CombatModifier::WeaponDefense, victim.blockWeapon->defense);
                blockDefense = steps.Apply(CombatSide::Attacker, CombatModifier::EnemyWeaponDefense, blockDefense);
                shield       = victim.blockWeapon->weaponClass == kShieldWeaponClass;
            }
        }

        // The attack, then the swing's own scaling (`CalculateSwingAttack` 0x180727DD0).
        std::array<float, 3> attack = attacker.weapon != nullptr ? WeaponAttack(steps, attacker) : UnarmedAttack(steps, attacker, blow.attackTypeId);
        float mount                 = 1.0F;
        if (blow.mounted) {
            const float speed = std::min(std::max(blow.mountedSpeed / CombatPricingParams::kHorseAttackMaxSpeed, 0.0F), 1.0F);
            mount             = steps.Apply(CombatSide::Attacker, CombatModifier::HorseMountedAttack, 1.0F + (CombatPricingParams::kHorseMaxAttackCoef - 1.0F) * (speed * speed));
        }
        const float energy = std::min(std::max(1.0F - std::pow(1.0F - blow.energy, CombatPricingParams::kAttackEnergyModifier), 0.0F), 1.0F);
        const float effort = energy * blow.staminaCoefficient;
        const float charge = blow.attackCharge * steps.Apply(CombatSide::Attacker, CombatModifier::AttackChargeMod, CombatPricingParams::kAttackChargingAttMod) + 1.0F;
        for (float &value : attack) {
            value = ((effort * value) * mount) * charge;
            if (blow.attackCoefficient != 1.0F) {
                value *= blow.attackCoefficient;
            }
            value *= scales.attack;
        }
        price.attack = attack;

        // The armour at the struck subpart; one with none is the upper front body.
        const std::int32_t subpart   = blow.hitSubpart >= 0 ? blow.hitSubpart : 1;
        const float bodyBase         = modifiers.BodyBaseArmor(victim.archetype->bodyBaseArmor);
        std::array<float, 3> defense = SubpartArmor(victim, subpart);
        for (float &value : defense) {
            value += bodyBase;
            value = steps.Apply(CombatSide::Attacker, CombatModifier::OpponentDefense, steps.Apply(CombatSide::Victim, CombatModifier::ArmorDefense, value));
            value = std::max(value * scales.defense, 0.0F);
        }
        price.armor = defense;

        float block = 0.0F;
        if (blockDefense > 0.0F) {
            const float skill = std::min(std::max(steps.Apply(CombatSide::Attacker, CombatModifier::OpponentDefenseSkill, victim.defenseSkill), 0.0F), CombatPricingParams::kSkillCap);
            block             = (skill * CombatPricingParams::kSkillToDefense + 1.0F) * blockDefense * scales.block;
        }
        price.block              = block;
        price.subpartCoefficient = SubpartCoefficient(steps, blow.hitSubpart);

        // One pass per damage type, each seeing the stamina the last one left.
        float stamina          = victim.stamina;
        float totalStamina     = 0.0F;
        float totalHealth      = 0.0F;
        const bool perfect     = blow.perfectBlock;
        const float fromBehind = std::fabs(blow.angle) > CombatPricingParams::kFromBehindAngle ? (modifiers.HasAbility(CombatModifierSource::kEutuBrutusAbility) ? CombatPricingParams::kPerkBrutusDmgRBonusFromBehind : 1.0F) * CombatPricingParams::kCombatDmgRBonusFromBehind : 0.0F;
        for (std::size_t i = 0; i < 3; ++i) {
            if ((price.damageTypes & (1 << i)) == 0) {
                continue;
            }
            float passAttack = attack[i];
            if (blow.counterAttack) {
                passAttack = steps.Apply(CombatSide::Attacker, CombatModifier::CounterAttack, passAttack);
            }
            float passDefense = defense[i];
            float fraction    = 0.0F;
            float dmgR        = 0.0F;
            if (perfect) {
                passDefense = block;
                fraction    = 1.0F;
            }
            else if (blockDefense > 0.0F) {
                float cost = StaminaDamage(passAttack - block, blow.weakBlock, false);
                if (shield) {
                    cost = steps.Apply(CombatSide::Victim, CombatModifier::ShieldStaminaLoss, cost);
                }
                cost             = steps.Apply(CombatSide::Victim, CombatModifier::StaminaLoss, cost);
                cost             = steps.Apply(CombatSide::Attacker, CombatModifier::OpponentStaminaBlocked, cost);
                const float held = stamina / cost;
                if (passAttack - block > 0.0F || held < 1.0F || blow.blockBroken) {
                    if (held * 3.0F > 1.0F) {
                        fraction    = 0.33333334F;
                        passDefense = block / 3.0F + passDefense;
                    }
                }
                else {
                    passDefense = block;
                    fraction    = 1.0F;
                }
            }
            if (!perfect) {
                dmgR = passAttack - passDefense;
                if (fromBehind != 0.0F) {
                    dmgR = dmgR * fromBehind;
                }
            }

            // `FinishDamageTypePass` (0x180C663E8).
            float passStamina = StaminaDamage(dmgR, blow.weakBlock, perfect);
            if (shield && fraction == 1.0F && !perfect) {
                passStamina = steps.Apply(CombatSide::Victim, CombatModifier::ShieldStaminaLoss, passStamina);
            }
            passStamina = steps.Apply(CombatSide::Victim, CombatModifier::StaminaLoss, passStamina);
            if (blockDefense > 0.0F && !perfect) {
                passStamina = steps.Apply(CombatSide::Attacker, CombatModifier::OpponentStaminaBlocked, passStamina);
            }
            float passHealth = HealthDamage(dmgR, stamina, passStamina) * price.subpartCoefficient;
            passHealth       = steps.Apply(CombatSide::Victim, CombatModifier::HealthLoss, passHealth);
            totalStamina += passStamina;
            totalHealth += passHealth;
            stamina = std::min(std::max(stamina - passStamina, 0.0F), victim.staminaMaximum);
        }

        // `ClampFinalDamage` (0x180C639B8): every blow but a perfect block costs at least the minimum.
        if (!perfect) {
            totalStamina = std::max(std::min(totalStamina, CombatPricingParams::kMaxStaminaDamage), CombatPricingParams::kMinStaminaDamage);
            totalHealth  = totalHealth > 0.0F ? totalHealth * CombatPricingParams::kDmgRToHealthCoefA : totalHealth;
            totalHealth  = std::max(std::min(totalHealth, CombatPricingParams::kMaxDamage), 0.0F);
        }
        price.health  = std::max(totalHealth * scales.health, 0.0F);
        price.stamina = std::max(totalStamina, 0.0F);
        if (!std::isfinite(price.health)) {
            price.health = 0.0F;
        }
        if (!std::isfinite(price.stamina)) {
            price.stamina = 0.0F;
        }
        return price;
    }
} // namespace KCDC::Shared::Game
