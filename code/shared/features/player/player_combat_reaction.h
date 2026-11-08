#pragma once

#include <mafianet/bit_stream.h>
#include <mafianet/time.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A combat action a player's machine ran on its own body, other than the
     * swing the CombatAttack stream already carries, relayed by the exact
     * database rows the engine built it from.
     *
     * A melee contact is resolved on exactly one machine: the one where the
     * victim is the real body. That machine publishes what its engine ran on
     * the victim -- the hit reaction, its blocks and perfect blocks -- here,
     * and reports the blow itself to the server (`CombatHitReport`), whose
     * ruling carries everything that lands on the other body: the
     * attacker's recoil, the impact, the health. Nothing here ever names
     * another player's body as the one to act on.
     *
     * Rides the sender's presentation stream, so it stays in order with the
     * sender's own swings. Sent with `actorNetworkId` at zero; the server
     * fills it in from the sending connection.
     */
    struct CombatReaction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatReaction");

        enum Kind : std::uint8_t {
            /** The sender perfect-blocked; `rowIndex` is the perfect-block row, the riposte candidate rides with it. */
            PerfectBlock = 0,

            /** The sender's sync perfect block or master strike; `pairedRowIndex` is the partner half's row. */
            SyncPerfectBlock = 1,

            /** The sender's body reacted to `opponentNetworkId`'s blow; `rowIndex` is the hit row. */
            Hit = 2,

            /** An attack the sender's engine started outside the swing input -- a combo -- by its attack row. */
            Attack = 3,

            /**
             * The sender raised the counter of the perfect block it is
             * playing against `opponentNetworkId`: the strike-back plays
             * inside the perfect-block action (`OnRiposteRequestChanged`,
             * 0x1819BEFD0) rather than as an attack of its own. `rowIndex`,
             * `pairedRowIndex` and `variantFlags` are that perfect block's,
             * as PerfectBlock carries them; the strike takes a generation.
             */
            Riposte = 4,

            /**
             * The sender blocked `opponentNetworkId`'s swing; `rowIndex` is
             * the block row, `attackGeneration` that swing's generation as
             * the sender saw it, or zero when it saw none.
             */
            Block = 5,

            /**
             * The sender dodged around `opponentNetworkId`; `rowIndex` is the
             * dodge row its engine picked, `direction` what the dodge was
             * asked for.
             */
            Dodge = 6,
        };

        std::uint8_t kind                  = PerfectBlock;
        std::uint32_t incarnation          = 0;
        std::uint8_t epoch                 = 0;
        std::uint32_t combatRevision       = 0;
        std::uint64_t presentationSequence = 0;
        bool valid                         = true;

        /** The player whose machine ran it. Filled in by the server, never by a client. */
        std::uint64_t actorNetworkId = 0;

        /**
         * The fighter on the other side of it: the attacker of a Hit, the
         * opponent of a perfect block. Every kind but Attack needs one -- the
         * engine builds none of them without an opponent.
         */
        std::uint64_t opponentNetworkId = 0;

        static constexpr std::uint32_t kInvalidRowIndex = UINT32_MAX;

        /** The row in the table the kind names. */
        std::uint32_t rowIndex = kInvalidRowIndex;

        /** PerfectBlock and Riposte: the riposte's attack row. SyncPerfectBlock: the partner's sync-hit row. */
        std::uint32_t pairedRowIndex = kInvalidRowIndex;

        /** Attack only: the attack candidate's bytes. */
        std::uint8_t candidateFlag0 = 0;
        std::uint8_t candidateFlag1 = 0;

        /**
         * PerfectBlock and Riposte: the perfect block's own candidate flag. SyncPerfectBlock: 1
         * for the lethal variant. Hit: the two bits the hit record lent it.
         */
        std::uint8_t variantFlags = 0;

        /** Attack only: the accepted presentation context, as CombatAttack carries it. */
        std::uint8_t contextFlag0 = 0;
        std::uint8_t contextFlag1 = 0;
        std::uint32_t stateMode   = 0;
        float attackSpeed         = 0.0F;

        /**
         * Attack: the swing's generation, from the same count the sender's
         * input swings take theirs from, so a blow it lands is named the same
         * way whichever started it. Block: the generation of the swing it
         * met.
         */
        std::uint32_t attackGeneration = 0;

        /** Attack only: how long after it started the blade can still land (`CombatSwingWindowMs`). */
        std::uint32_t windowMs = 0;

        /** Dodge only: the input direction the dodge was asked for, in the body's own frame, unit length. */
        float direction[2] = {0.0F, 0.0F};

        /**
         * When the server relayed this, on the server's clock; zero on the
         * way up. A receiver shifts it onto its own clock -- the one the
         * actor's relayed poses are stamped on -- and plays it once the body
         * is drawn at that moment, rather than ahead of the pose.
         */
        MafiaNet::Time relayedAt = 0;

        /** The same wire bound CombatAttack puts on the value. */
        static constexpr float kMaxAttackSpeed = 8.0F;

        /** A dodge's direction is a unit vector; this bounds it with room to spare. */
        static constexpr float kMaxDirectionLengthSquared = 4.0F;

        /**
         * How long a master strike's blade can still land after its owner's
         * engine started it. A sync perfect block strikes the attacker back,
         * so it takes a generation from the swing count like any swing; its
         * rows carry no attack timing, and this bounds the pair's whole run.
         */
        static constexpr std::uint32_t kSyncStrikeWindowMs = 2500;

        /** What a receiver needs of the other fighter before it may build the action. */
        enum class OpponentRequirement : std::uint8_t {
            /** A swing needs nobody: the attack builds from its row alone. */
            None,

            /** A hit reaction names its attacker, who has to be here. */
            Present,

            /**
             * A perfect block -- and a riposte, which may rebuild its perfect
             * block -- and a sync defense read their owner's opponent
             * -- the alignment unchecked, the sync partner by running on it --
             * a block is only ever fired at the owner's striking opponent
             * (`FireSlotAction`, 0x181483580), and a dodge's query circles the
             * opponent it is asked about -- so it has to be the named fighter
             * already.
             */
            CurrentOpponent,
        };

        OpponentRequirement RequiredOpponent() const {
            switch (kind) {
            case PerfectBlock:
            case SyncPerfectBlock:
            case Riposte:
            case Block:
            case Dodge: return OpponentRequirement::CurrentOpponent;
            case Hit: return OpponentRequirement::Present;
            default: return OpponentRequirement::None;
            }
        }

        /**
         * Whether the other fighter it names, if any, is someone else in the
         * sender's world -- the one thing a server can check of a reaction
         * whose rows only the sender's engine can vouch for.
         * `isOtherPlayerInWorld` answers for one network id.
         */
        template <typename TIsOtherPlayerInWorld>
        bool NamesOnlyOtherPlayers(TIsOtherPlayerInWorld &&isOtherPlayerInWorld) const {
            return opponentNetworkId == 0 || isOtherPlayerInWorld(opponentNetworkId);
        }

        const char *KindName() const {
            switch (kind) {
            case PerfectBlock: return "PerfectBlock";
            case SyncPerfectBlock: return "SyncPerfectBlock";
            case Hit: return "Hit";
            case Attack: return "Attack";
            case Riposte: return "Riposte";
            case Block: return "Block";
            case Dodge: return "Dodge";
            default: return "Invalid";
            }
        }

        bool IsSane() const {
            if (!valid || kind > Dodge || incarnation == 0 || combatRevision == 0 || presentationSequence == 0 || rowIndex == kInvalidRowIndex) {
                return false;
            }
            if (kind != Attack && opponentNetworkId == 0) {
                return false;
            }
            if ((kind == PerfectBlock || kind == SyncPerfectBlock || kind == Riposte) && pairedRowIndex == kInvalidRowIndex) {
                return false;
            }
            if (kind == Attack && (!(stateMode == 3 || stateMode == 4) || attackGeneration == 0)) {
                return false;
            }
            if (kind == Riposte && attackGeneration == 0) {
                return false;
            }
            if (!std::isfinite(direction[0]) || !std::isfinite(direction[1]) || direction[0] * direction[0] + direction[1] * direction[1] > kMaxDirectionLengthSquared) {
                return false;
            }
            return candidateFlag0 <= 1 && candidateFlag1 <= 1 && variantFlags <= 3 && contextFlag0 <= 1 && contextFlag1 <= 1 && std::isfinite(attackSpeed) && attackSpeed >= 0.0F && attackSpeed <= kMaxAttackSpeed;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, kind) && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, combatRevision) && bs->Serialize(write, presentationSequence) && bs->Serialize(write, actorNetworkId)
                    && bs->Serialize(write, opponentNetworkId) && bs->Serialize(write, rowIndex) && bs->Serialize(write, pairedRowIndex) && bs->Serialize(write, candidateFlag0) && bs->Serialize(write, candidateFlag1) && bs->Serialize(write, variantFlags)
                    && bs->Serialize(write, contextFlag0) && bs->Serialize(write, contextFlag1) && bs->Serialize(write, stateMode) && bs->Serialize(write, attackSpeed) && bs->Serialize(write, attackGeneration) && bs->Serialize(write, windowMs)
                    && bs->Serialize(write, direction[0]) && bs->Serialize(write, direction[1]) && bs->Serialize(write, relayedAt);
        }
    };
} // namespace KCDC::Shared::RPC
