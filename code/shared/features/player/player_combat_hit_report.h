#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * The sound and sparks of a blow, as the machine that resolved it played
     * them.
     *
     * The engine plays a contact's impact as one material effect, picked from
     * the hit record. A machine that drops the contact has no record, so it
     * plays no impact at all. These are the record fields that pick the effect
     * and place it. Each receiver fills in its own two bodies and the
     * attacker's own weapon, and plays it through the same native call.
     */
    struct CombatImpact {
        /** Further than any blade reaches from the body it struck. */
        static constexpr float kMaxOffset = 4.0F;

        std::uint8_t weaponKind  = 0;
        std::uint8_t hitType     = 0;
        std::uint8_t attackStyle = 0;

        /** Whether the blow took health: a `passed` row rather than a `stopped` one. */
        bool passed = false;

        std::int16_t sourceSurfaceId = -1;
        std::int16_t targetSurfaceId = -1;
        std::int16_t bodyMaterialId  = -1;
        std::int16_t partId          = -1;

        /** A joint of the attacker's, when the contact named no part of the victim. */
        std::int16_t attackerJointId = -1;

        /** Where it struck, relative to the victim's position; world axes. */
        float offset[3]    = {0.0F, 0.0F, 0.0F};
        float normal[3]    = {0.0F, 0.0F, 1.0F};
        float direction[3] = {0.0F, 0.0F, 1.0F};

        bool IsSane() const {
            const auto finiteWithin = [](const float (&v)[3], float bound) {
                return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::fabs(v[0]) <= bound && std::fabs(v[1]) <= bound && std::fabs(v[2]) <= bound;
            };
            return finiteWithin(offset, kMaxOffset) && finiteWithin(normal, 1.5F) && finiteWithin(direction, 1.5F);
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return bs->Serialize(write, weaponKind) && bs->Serialize(write, hitType) && bs->Serialize(write, attackStyle) && bs->Serialize(write, passed) && bs->Serialize(write, sourceSurfaceId) && bs->Serialize(write, targetSurfaceId) && bs->Serialize(write, bodyMaterialId)
                   && bs->Serialize(write, partId) && bs->Serialize(write, attackerJointId) && bs->Serialize(write, offset[0]) && bs->Serialize(write, offset[1]) && bs->Serialize(write, offset[2]) && bs->Serialize(write, normal[0]) && bs->Serialize(write, normal[1])
                   && bs->Serialize(write, normal[2]) && bs->Serialize(write, direction[0]) && bs->Serialize(write, direction[1]) && bs->Serialize(write, direction[2]);
        }
    };

    /** What dealt a blow the referee rules on. */
    enum class CombatHitSource : std::uint8_t {
        /** A swing, named by the attacker's attack generation. */
        Melee = 0,

        /** An arrow or a bolt, named by the shooter's shot sequence. */
        Missile = 1,
    };

    /**
     * How the victim's engine took a melee blow, from the hit data its
     * calculation ran on (`S_CombatHitData` +0x54..+0x57). A missile carries
     * none of these.
     */
    struct CombatHitFlags {
        static constexpr std::uint8_t kBlocked      = 1U << 0;
        static constexpr std::uint8_t kWeakBlock    = 1U << 1;
        static constexpr std::uint8_t kPerfectBlock = 1U << 2;
        static constexpr std::uint8_t kBlockBroken  = 1U << 3;
        static constexpr std::uint8_t kAll          = kBlocked | kWeakBlock | kPerfectBlock | kBlockBroken;
    };

    /**
     * What the referee prices a melee blow from that only the contact knows:
     * the hit data the resolving engine built (`S_CombatHitData`) and the
     * victim's stamina as the blow met it. Everything else -- the weapon, the
     * armour, the skills -- the server holds itself.
     */
    struct CombatBlowInputs {
        /** The three damage passes together. */
        static constexpr std::uint8_t kAllDamageTypes = 7;

        /** The bounds each value is held to: past them, no contact the engine raises. */
        static constexpr float kMaxRelativeSpeed     = 100.0F;
        static constexpr float kMaxAttackCoefficient = 10.0F;
        static constexpr float kMaxAngle             = 7.0F;
        static constexpr float kMaxStamina           = 100000.0F;

        /** Stab 1, slash 2, smash 4: the passes the engine ran (+0x24). */
        std::uint8_t damageTypes = 0;

        /** The attacker's stamina coefficient for the swing (+0x2C), 0..1. */
        float staminaCoefficient = 1.0F;

        /** The contact's energy (+0x30), 0..1. */
        float energy = 1.0F;

        /** The length of the blade's velocity against the victim (+0x34), metres a second. */
        float relativeSpeed = 0.0F;

        /** The hit entry's own attack coefficient (+0x6C). */
        float attackCoefficient = 1.0F;

        /** From the victim to the attacker, radians (+0x70). */
        float angle = 0.0F;

        bool mounted       = false; // +0x5F
        bool counterAttack = false; // +0x5E

        /**
         * The attacker's `AttackCharge` (derived stat 118) as the swing met the
         * victim: its counter request was up, which is a riposte's strike-back.
         */
        bool attackCharged = false;

        /** The victim's stamina when the blow met it, and its ceiling. */
        float victimStamina        = 0.0F;
        float victimStaminaMaximum = 0.0F;

        bool IsSane() const {
            const auto within = [](float value, float low, float high) {
                return std::isfinite(value) && value >= low && value <= high;
            };
            return damageTypes <= kAllDamageTypes && within(staminaCoefficient, 0.0F, 1.0F) && within(energy, 0.0F, 1.0F) && within(relativeSpeed, 0.0F, kMaxRelativeSpeed) && within(attackCoefficient, 0.0F, kMaxAttackCoefficient) && within(angle, -kMaxAngle, kMaxAngle)
                   && within(victimStamina, 0.0F, kMaxStamina) && within(victimStaminaMaximum, 0.0F, kMaxStamina);
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return bs->Serialize(write, damageTypes) && bs->Serialize(write, staminaCoefficient) && bs->Serialize(write, energy) && bs->Serialize(write, relativeSpeed) && bs->Serialize(write, attackCoefficient) && bs->Serialize(write, angle) && bs->Serialize(write, mounted)
                   && bs->Serialize(write, counterAttack) && bs->Serialize(write, attackCharged) && bs->Serialize(write, victimStamina) && bs->Serialize(write, victimStaminaMaximum);
        }
    };

    /** Which side of a blow resolved it on its own machine. */
    enum class CombatHitResolver : std::uint8_t {
        /** The victim's machine: its copy of the attacker's blade met the real body, against the real guard. */
        Victim = 0,

        /** The attacker's machine: the real blade met its copy of the victim, against the victim's replicated guard. */
        Attacker = 1,
    };

    /**
     * A blow one player landed on another, as one of their two machines
     * resolved it -- the victim's report, or the attacker's claim.
     *
     * **The victim's report.** A remote player's blow landed on this
     * machine's own body, and nothing of it was taken.
     *
     * The victim's engine resolved the contact against the guard the victim
     * really held and computed the blow with the attacker's own soul answers
     * replayed into it, so the numbers here are the game's own. The health it
     * would have taken was withheld at the soul's storage writer; the server
     * rules on the blow and every machine plays the ruling, the victim's
     * included. Stamina was spent where it fell: it decides the victim's next
     * block, which cannot wait a round trip.
     *
     * The server takes the victim from the sending connection. The attacker
     * and the swing are this client's claim, which the server checks against
     * the swings it relayed and the shots it accepted.
     *
     * **The attacker's claim.** The attacker's real blade met its copy of the
     * victim -- a contact the attacker's engine raises from its own swing,
     * whether or not the victim's copy of that swing ever meets the real
     * body -- and the attacker's engine resolved it against the guard that
     * copy holds. The server holds a claim until the victim's own report of
     * the same swing can have arrived, and rules on the claim only when none
     * did: the victim's machine is never wrong about its own guard, and the
     * claim makes sure no blow that landed is lost when its copy missed.
     * `attackerNetworkId` is the sender's own; the server checks it.
     */
    struct CombatHitReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatHitReport");

        std::uint8_t source = static_cast<std::uint8_t>(CombatHitSource::Melee);

        /** A `CombatHitResolver`: a victim's report, or an attacker's claim. */
        std::uint8_t resolver = static_cast<std::uint8_t>(CombatHitResolver::Victim);

        /** The sender's own identity fence: the victim's for a report, the attacker's for a claim. */
        // Zero reporter keeps the player protocol; an NPC names its simulator-owned body.
        std::uint64_t reporterNetworkId   = 0;
        std::uint32_t reporterRevision    = 0;
        std::uint32_t attackerIncarnation = 0;
        std::uint32_t attackerRevision    = 0;
        std::uint32_t incarnation         = 0;
        std::uint8_t epoch                = 0;

        /** A claim's victim; zero on a report, whose victim is its sender. */
        std::uint64_t victimNetworkId = 0;

        /** The player or NPC whose blow it was. */
        std::uint64_t attackerNetworkId = 0;

        /** Melee: the attack generation the attacker's body was swinging here. */
        std::uint32_t attackGeneration = 0;

        /** Missile: the shooter's shot sequence. */
        std::uint64_t shotSequence = 0;

        /** Health the victim's engine computed and withheld; zero for a blow that took none. */
        float healthLoss = 0.0F;

        /** Stamina the blow cost the victim, already spent. */
        float staminaLoss = 0.0F;

        /** Whether the withheld write would have killed: what a blow that cannot kill never does. */
        bool engineLethal = false;

        /** A `body_subpart.xml` id, or -1. */
        std::int16_t hitSubpart = -1;

        /** A `Game::DamageReason`, as the engine wrote it into the damage effect. */
        std::uint8_t reason = 0;

        /** `CombatHitFlags`. */
        std::uint8_t flags = 0;

        /** The `combat_attack_type` row the blow raised, or -1. */
        std::int32_t attackTypeId = -1;

        static constexpr std::uint32_t kNoRow = UINT32_MAX;

        /**
         * The recoil the victim's engine ran on its copy of the attacker -- a
         * row of `CombatDb.FailedAttack` -- or `kNoRow`. The attacker's machine
         * runs it on the real body once the server has ruled, and everyone
         * else on theirs.
         */
        std::uint32_t recoilRowIndex = kNoRow;

        /**
         * A claim's hit reaction: the `CombatDb.Hit` row the attacker's engine
         * ran on its copy of the victim, and the two bits of the hit record it
         * lent it, or `kNoRow`. A victim's own reaction rides its own stream.
         */
        std::uint32_t hitRowIndex   = kNoRow;
        std::uint8_t hitRecordFlags = 0;

        bool hasImpact = false;
        CombatImpact impact;

        /** A swing's hit data, once the engine built it: what the referee prices the blow from. */
        bool hasInputs = false;
        CombatBlowInputs inputs;

        /** Health a single blow can take: the engine's own clamp is 200 (`MaxDamage`); an arrow's lethal multiplier is the reason for the headroom. */
        static constexpr float kMaxHealthLoss = 10000.0F;

        /** Stamina: three damage passes, each clamped by the engine to 40. */
        static constexpr float kMaxStaminaLoss = 1000.0F;

        bool valid = true;

        CombatHitSource Source() const {
            return static_cast<CombatHitSource>(source);
        }

        bool IsClaim() const {
            return resolver == static_cast<std::uint8_t>(CombatHitResolver::Attacker);
        }

        /**
         * A contact that carries no blow: no attack type, no damage pass and
         * no health. A master strike's pair touches its victim like this
         * before the strike itself lands; ruled on, the touch would use up
         * the swing and the strike be set aside as already struck.
         */
        bool IsEmptyTouch() const {
            return Source() == CombatHitSource::Melee && hasInputs && attackTypeId < 0 && inputs.damageTypes == 0 && healthLoss <= 0.0F && (flags & CombatHitFlags::kAll) == 0;
        }

        /**
         * Whether a contact came to a blow worth ruling on: the engine built
         * hit data for it, it took something, or it ran a reaction or an
         * impact. A contact that resolved into nothing -- the same swing's
         * second handler finding the blow already taken -- is not one.
         */
        bool CarriesBlow() const {
            return (hasInputs || healthLoss > 0.0F || staminaLoss > 0.0F || recoilRowIndex != kNoRow || hitRowIndex != kNoRow || hasImpact) && !IsEmptyTouch();
        }

        bool IsSane() const {
            if ((reporterNetworkId != 0 && (IsClaim() || reporterRevision == 0 || reporterNetworkId == attackerNetworkId)) || (reporterNetworkId == 0 && reporterRevision != 0) || ((attackerIncarnation == 0) != (attackerRevision == 0))) {
                return false;
            }
            if (!valid || source > static_cast<std::uint8_t>(CombatHitSource::Missile) || resolver > static_cast<std::uint8_t>(CombatHitResolver::Attacker) || incarnation == 0 || attackerNetworkId == 0) {
                return false;
            }
            // A claim names its victim and is only ever a blade's; a report's
            // victim is its sender, and it has no reaction of the victim's to
            // carry -- that rides the victim's own stream.
            if (IsClaim() ? (victimNetworkId == 0 || victimNetworkId == attackerNetworkId || Source() != CombatHitSource::Melee) : (victimNetworkId != 0 || hitRowIndex != kNoRow)) {
                return false;
            }
            if (hitRecordFlags > 3) {
                return false;
            }
            if (Source() == CombatHitSource::Melee ? attackGeneration == 0 : shotSequence == 0) {
                return false;
            }
            if (Source() == CombatHitSource::Missile && (flags != 0 || recoilRowIndex != kNoRow)) {
                return false;
            }
            if ((flags & ~CombatHitFlags::kAll) != 0 || hitSubpart < -1) {
                return false;
            }
            if (hasImpact && !impact.IsSane()) {
                return false;
            }
            if (hasInputs && (Source() != CombatHitSource::Melee || !inputs.IsSane())) {
                return false;
            }
            return std::isfinite(healthLoss) && healthLoss >= 0.0F && healthLoss <= kMaxHealthLoss && std::isfinite(staminaLoss) && staminaLoss >= 0.0F && staminaLoss <= kMaxStaminaLoss;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, reporterNetworkId) && bs->Serialize(write, reporterRevision) && bs->Serialize(write, attackerIncarnation) && bs->Serialize(write, attackerRevision) && bs->Serialize(write, source) && bs->Serialize(write, resolver)
                    && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, victimNetworkId) && bs->Serialize(write, attackerNetworkId) && bs->Serialize(write, attackGeneration) && bs->Serialize(write, shotSequence)
                    && bs->Serialize(write, healthLoss) && bs->Serialize(write, staminaLoss) && bs->Serialize(write, engineLethal) && bs->Serialize(write, hitSubpart) && bs->Serialize(write, reason) && bs->Serialize(write, flags) && bs->Serialize(write, attackTypeId)
                    && bs->Serialize(write, recoilRowIndex) && bs->Serialize(write, hitRowIndex) && bs->Serialize(write, hitRecordFlags) && bs->Serialize(write, hasImpact) && (!hasImpact || impact.Serialize(bs, write)) && bs->Serialize(write, hasInputs)
                    && (!hasInputs || inputs.Serialize(bs, write));
        }
    };
} // namespace KCDC::Shared::RPC
