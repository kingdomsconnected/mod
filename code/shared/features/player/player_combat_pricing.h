#pragma once

#include "shared/game/combat_catalog.h"

#include <array>
#include <cstdint>
#include <vector>

/**
 * What one melee blow takes, worked out the way the game's own hit
 * calculation works it out (`C_HitCalculation`, WHGame.dll 0x18072704C), from
 * the authored tables and what the server holds of the two fighters.
 *
 * Every step is the engine's, in its order and in single precision; the
 * addresses name where each one was read. What the engine reads of a soul's
 * buffs and perks goes through `CombatModifierSource`, one call per
 * `ApplyModifiers` the engine makes, so a caller decides what of a soul it
 * can vouch for. Nothing here touches a body or a wire.
 */
namespace KCDC::Shared::Game {
    /** The engine's RPG constants a blow reads: `rpg_param.xml` over the code defaults (`InitRPGParamDefaults` 0x1810BC164). */
    struct CombatPricingParams final {
        static constexpr float kWeaponStatusToAttackCoef      = 0.6F;   // 26
        static constexpr float kArmorStatusToDefenseCoef      = 0.6F;   // 27
        static constexpr float kHorseMaxAttackCoef            = 2.5F;   // 34
        static constexpr float kHorseAttackMaxSpeed           = 7.0F;   // 35
        static constexpr float kSkillCap                      = 30.0F;  // 45
        static constexpr float kMinStatToAttackMult           = 0.5F;   // 55
        static constexpr float kMaxStatToAttackMult           = 1.1F;   // 56
        static constexpr float kMaxStatToAttackStatDiff       = 10.0F;  // 57
        static constexpr float kSkillToDmgConstA              = 250.0F; // 58
        static constexpr float kSkillToDefense                = 0.028571429F;
        static constexpr float kMaxDamage                     = 200.0F; // 181
        static constexpr float kMinStaminaDamage              = 15.0F;  // 185
        static constexpr float kMaxStaminaDamage              = 40.0F;  // 186
        static constexpr float kMinStaminaDmgR                = -50.0F; // 187
        static constexpr float kMaxStaminaDmgR                = 80.0F;  // 188
        static constexpr float kAverageArmorDefenseWeight     = 0.25F;  // 191
        static constexpr float kWeakBlockStamCoef             = 2.0F;   // 193
        static constexpr float kDmgRToHealthCoefA             = 7.0F;   // 195
        static constexpr float kDmgRToHealthCoefB             = 0.05F;  // 196
        static constexpr float kStaminaDeficitHealthDamageA   = 0.05F;  // 197
        static constexpr float kStaminaDeficitHealthDamageB   = 0.075F; // 198
        static constexpr float kAttackEnergyModifier          = 2.0F;   // 207
        static constexpr float kUnarmedBlockDefense           = 3.0F;   // 213
        static constexpr float kUnarmedAttackReqStrBase       = 2.0F;   // 214
        static constexpr float kCombatDmgRBonusFromBehind     = 1.2F;   // 217
        static constexpr float kFromBehindAngle               = 2.3561945F;
        static constexpr float kAttackChargingAttMod          = 0.1F; // 229
        static constexpr float kPerkBrutusDmgRBonusFromBehind = 1.3F; // 893
    };

    /**
     * A buff parameter the hit calculation feeds a value through: the
     * `buff_params` code, which is `E_SoulProperty - 311`. Each names the soul
     * whose modifiers apply, in the order `CombatPricing` meets them.
     */
    enum class CombatModifier : std::uint8_t {
        ArmorDefense           = 0,   // ade, victim: armour at the subpart
        UnarmedAttack          = 1,   // uat, attacker
        WeaponAttack           = 2,   // wat, attacker
        HorseMountedAttack     = 4,   // ahm, attacker
        HealthLoss             = 10,  // hlh, victim
        StaminaLoss            = 11,  // slh, victim
        ShieldStaminaLoss      = 12,  // sls, victim
        OpponentStaminaBlocked = 34,  // osb, attacker
        HeadDamageDealt        = 51,  // dtd, attacker
        HeadDamageTaken        = 52,  // tdh, victim
        SlashAttack            = 54,  // sla, attacker
        StabAttack             = 55,  // sta, attacker
        SmashAttack            = 56,  // ssa, attacker
        WeaponStrengthReq      = 57,  // wsr, attacker
        WeaponStrength         = 59,  // wrs, attacker
        WeaponDefense          = 63,  // wde, victim: the blocking weapon
        EnemyWeaponDefense     = 64,  // ewd, attacker: the victim's blocking weapon
        CounterAttack          = 68,  // cat, attacker
        AttackChargeMod        = 69,  // acm, attacker
        OpponentDefense        = 77,  // ode, attacker: the victim's armour
        OpponentDefenseSkill   = 102, // opd, attacker: the victim's defence skill
    };

    /** Which fighter's soul a modifier is read off. */
    enum class CombatSide : std::uint8_t {
        Attacker,
        Victim,
    };

    /**
     * The buffs and perks of the two souls, as one `ApplyModifiers` call
     * (0x180649F1C) at a time. The default applies none. Whatever comes back
     * is clamped at zero, as the engine clamps every id a blow reads.
     */
    class CombatModifierSource {
      public:
        virtual ~CombatModifierSource() = default;

        virtual float Apply(CombatSide side, CombatModifier modifier, float value) const {
            (void)side;
            (void)modifier;
            return value;
        }

        /** `BodyBaseArmor` (derived stat 89) of the victim from its archetype's base. */
        virtual float BodyBaseArmor(float base) const {
            return base;
        }

        /** The `EutuBrutus` soul ability: a stronger blow from behind. */
        static constexpr std::uint32_t kEutuBrutusAbility = 36;

        /** Whether the attacker has a soul ability (`perk_soul_ability`). */
        virtual bool HasAbility(std::uint32_t ability) const {
            (void)ability;
            return false;
        }
    };

    /** One piece the victim wears, and its condition. */
    struct CombatWornArmor {
        const KCDC::Shared::Game::CombatArmorEntry *armor = nullptr;

        /** Item health 0..1; -1 for an item never worn down. */
        float health = -1.0F;
    };

    /** The attacker as a blow reads it. */
    struct CombatPricingAttacker {
        /** The weapon at the hit data's +0x78, or null for a bare hand. */
        const KCDC::Shared::Game::CombatWeaponEntry *weapon = nullptr;
        float weaponHealth                                  = -1.0F;

        /** Effective strength, and the skill the weapon's class trains (`weapon_unarmed` for a bare hand). */
        float strength = 0.0F;
        float skill    = 0.0F;

        const KCDC::Shared::Game::CombatSoulArchetype *archetype = &KCDC::Shared::Game::g_CombatHeroArchetype;
    };

    /** The victim as a blow reads it. */
    struct CombatPricingVictim {
        std::vector<CombatWornArmor> armor;

        /** What blocked, when something did: a weapon, or null for a bare-handed block. */
        const KCDC::Shared::Game::CombatWeaponEntry *blockWeapon = nullptr;

        /** Effective `defense` skill. */
        float defenseSkill = 0.0F;

        /** Stamina when the blow landed, and its ceiling. */
        float stamina        = 0.0F;
        float staminaMaximum = 0.0F;

        const KCDC::Shared::Game::CombatSoulArchetype *archetype = &KCDC::Shared::Game::g_CombatHeroArchetype;
    };

    /** The blow itself, from the hit data (`S_CombatHitData`) the engine built at contact. */
    struct CombatPricingBlow {
        std::int32_t attackTypeId = -1;

        /** The damage passes; 0 to derive them from the attack type and the weapon. */
        std::int32_t damageTypes = 0;

        float staminaCoefficient = 1.0F; // +0x2C
        float energy             = 1.0F; // +0x30
        float mountedSpeed       = 0.0F; // |+0x34..+0x3C|
        bool mounted             = false;
        bool blocked             = false;
        bool weakBlock           = false;
        bool perfectBlock        = false;
        bool blockBroken         = false;
        bool counterAttack       = false; // +0x5E
        // `AttackCharge` (derived stat 118): 1 for a riposte's strike-back,
        // whose counter request was up as it landed.
        float attackCharge      = 0.0F;
        std::int32_t hitSubpart = -1;
        float attackCoefficient = 1.0F; // +0x6C
        float angle             = 0.0F; // +0x70
    };

    /** What the gamemode scales a blow by on top of the game's arithmetic. 1 leaves a step as the game has it. */
    struct CombatPricingScales {
        float attack  = 1.0F;
        float defense = 1.0F;
        float block   = 1.0F;
        float health  = 1.0F;

        bool operator==(const CombatPricingScales &) const = default;
    };

    /** A priced blow, and the steps the price came from. */
    struct CombatPrice {
        float health  = 0.0F;
        float stamina = 0.0F;

        /** False for an attack type that knocks out rather than kills: a punch, a kick. */
        bool mayKill = true;

        /** The attack per damage pass (stab, slash, smash), after the swing's scaling. */
        std::array<float, 3> attack {};

        /** The victim's armour per pass at the struck subpart. */
        std::array<float, 3> armor {};

        /** What the block put up; 0 when nothing blocked. */
        float block = 0.0F;

        /** The struck subpart's coefficient. */
        float subpartCoefficient = 1.0F;

        std::int32_t damageTypes = 0;
    };

    /** The engine's 2-decimal rounding of a price factor (0x1804697AC). */
    float CombatRound2(float value);

    /** A condition multiplier for item health `health01` (0x180687444). */
    float CombatConditionFactor(float health01, float coefficient);

    /** `StatToAttackMult` (0x180687974). */
    float CombatStatToAttackMultiplier(float stat, float requirement);

    /** `Bias` (0x180473464). */
    float CombatBias(float x, float a);

    /** Prices one blow. */
    CombatPrice PriceMeleeBlow(const CombatPricingBlow &blow, const CombatPricingAttacker &attacker, const CombatPricingVictim &victim, const CombatModifierSource &modifiers, const CombatPricingScales &scales = {});
} // namespace KCDC::Shared::Game
