#pragma once

#include <mafianet/bit_stream.h>
#include <mafianet/time.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * One player threw a punch.
     *
     * Relayed as an event rather than replicated as a state: the receiver
     * plays the game's own attack action, so what is watched is the real
     * animation the real tables picked. The arm guard is the opposite and
     * rides the entity's delta channel instead.
     *
     * Sent with `attackerNetworkId` at zero; the server fills it in from the
     * sending connection, so a client can only make its own player swing.
     */
    struct CombatAttack {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatAttack");

        /** Which accepted lifecycle edge this message carries. */
        enum Phase : std::uint8_t {
            Press  = 0,
            Commit = 1,
            Cancel = 2,
        };

        std::uint8_t phase                 = Press;
        std::uint32_t incarnation          = 0;
        std::uint8_t epoch                 = 0;
        std::uint32_t combatRevision       = 0;
        std::uint64_t presentationSequence = 0;
        std::uint32_t attackGeneration     = 0;
        bool valid                         = true;

        /** The player who swung. Filled in by the server, never by a client. */
        std::uint64_t attackerNetworkId = 0;

        /**
         * The player the swing is aimed at -- the swinger's own opponent as
         * its engine held it at the edge -- or 0 for a swing at nobody. A
         * receiver squares its copy of the swinger up against that body
         * before playing the swing: the engine raises no contact for a
         * fighter with no opponent, and a lock-on, which is all the
         * replicated state carries, is not there in every fight.
         */
        std::uint64_t opponentNetworkId = 0;

        /**
         * The `combat_input_class` row the swing came from. Carried rather
         * than assumed: the same path serves the special attack and combos.
         */
        std::int32_t inputClassId = -1;

        /**
         * The direction the swing was thrown from. Sent with the event rather
         * than read off the replicated aim: a delta landing a tick later would
         * replay the punch from somewhere else.
         */
        std::int16_t zoneId = -1;

        /** No accepted attack-data row was supplied. */
        static constexpr std::uint32_t kInvalidAttackRowIndex = UINT32_MAX;

        /**
         * The exact immutable attack database row selected on the source.
         * Required only for Commit; Press and Cancel do not build an action.
         */
        std::uint32_t attackRowIndex = kInvalidAttackRowIndex;

        /** Proven bytes copied from the source's accepted attack candidate. */
        std::uint8_t candidateFlag0 = 0;
        std::uint8_t candidateFlag1 = 0;

        /** Proven presentation context copied from the accepted action. */
        std::uint8_t contextFlag0 = 0;
        std::uint8_t contextFlag1 = 0;
        std::uint32_t stateMode   = 0;

        /**
         * The time scale the sender's own swing timeline ran at, as
         * `C_CombatRPG::GetAttackSpeedMultiplier` answered it there, or 0 when
         * the sender did not see it. A receiver's engine would answer
         * differently -- the value depends on whether the swinger is
         * player-driven and on its opponent's soul -- so the receiver is
         * handed this one instead, and the same row reaches its hit at the
         * same moment on both machines. Commit only.
         */
        float attackSpeed = 0.0F;

        /**
         * How long after the commit the blade can still land, in
         * milliseconds: the row's own time to the hit at `attackSpeed`, then
         * its authored span to the withdraw (`CombatSwingWindowMs`). The
         * server holds every victim's report of this swing to it. Commit only.
         */
        std::uint32_t windowMs = 0;

        /**
         * When the server relayed this, on the server's clock; zero on the
         * way up. A receiver shifts it onto its own clock -- the one the
         * actor's relayed poses are stamped on -- and plays it once the body
         * is drawn at that moment, rather than ahead of the pose.
         */
        MafiaNet::Time relayedAt = 0;

        /** Wider than any skill profile yields; a bound on the wire, not a game rule. */
        static constexpr float kMaxAttackSpeed = 8.0F;

        const char *PhaseName() const {
            switch (phase) {
            case Press: return "Press";
            case Commit: return "Commit";
            case Cancel: return "Cancel";
            default: return "Invalid";
            }
        }

        bool IsSane() const {
            if (!valid || phase > Cancel || incarnation == 0 || combatRevision == 0 || presentationSequence == 0 || attackGeneration == 0) {
                return false;
            }
            if ((phase == Press || phase == Commit) && inputClassId < 0) {
                return false;
            }
            if (phase != Commit) {
                return true;
            }
            return attackRowIndex != kInvalidAttackRowIndex && std::isfinite(attackSpeed) && attackSpeed >= 0.0F && attackSpeed <= kMaxAttackSpeed && candidateFlag0 <= 1 && candidateFlag1 <= 1 && contextFlag0 <= 1 && contextFlag1 <= 1 && (stateMode == 3 || stateMode == 4);
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, phase) && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, combatRevision) && bs->Serialize(write, presentationSequence) && bs->Serialize(write, attackerNetworkId)
                    && bs->Serialize(write, opponentNetworkId) && bs->Serialize(write, attackGeneration) && bs->Serialize(write, inputClassId) && bs->Serialize(write, zoneId) && bs->Serialize(write, attackRowIndex) && bs->Serialize(write, candidateFlag0)
                    && bs->Serialize(write, candidateFlag1) && bs->Serialize(write, contextFlag0) && bs->Serialize(write, contextFlag1) && bs->Serialize(write, stateMode) && bs->Serialize(write, attackSpeed) && bs->Serialize(write, windowMs) && bs->Serialize(write, relayedAt);
        }
    };

    /** Stateful validation for one sender's accepted attack lifecycle. */
    struct CombatAttackLifecycle {
        enum class Result : std::uint8_t {
            Accepted,
            ActiveGenerationExists,
            TerminalWithoutPress,
            TerminalGenerationMismatch,
        };

        Result Accept(const CombatAttack &attack) {
            if (attack.phase == CombatAttack::Press) {
                if (activeGeneration != 0) {
                    return Result::ActiveGenerationExists;
                }
                activeGeneration = attack.attackGeneration;
                return Result::Accepted;
            }
            if (activeGeneration == 0) {
                return Result::TerminalWithoutPress;
            }
            if (activeGeneration != attack.attackGeneration) {
                return Result::TerminalGenerationMismatch;
            }
            activeGeneration = 0;
            return Result::Accepted;
        }

        void Reset() {
            activeGeneration = 0;
        }

        std::uint32_t activeGeneration = 0;
    };
} // namespace KCDC::Shared::RPC
