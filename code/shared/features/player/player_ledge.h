#pragma once

#include <mafianet/bit_stream.h>
#include <mafianet/time.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * One player vaulted over something.
     *
     * An event rather than a state -- the opposite choice to the ladder --
     * because a vault is over in about a second.
     *
     * Nothing describes the ledge. A ledge id is an index into an array the
     * engine builds as ledges stream in, so it names a different ledge on
     * every machine; the receiver finds the ledge again by re-running the
     * query at the puppet's own pose, which is already replicated, and
     * derives the rung and sprint variant from it as the sender's engine did.
     *
     * Sent with `climberNetworkId` at zero; the server fills it in from the
     * sending connection, so a client can only make its own player vault.
     */
    struct PlayerLedgeGrab {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerLedgeGrab");

        /** Position on the sender's one causal presentation stream. */
        std::uint64_t presentationSequence = 0;

        /** The player who vaulted. Filled in by the server, never a client. */
        std::uint64_t climberNetworkId = 0;

        std::uint32_t incarnation    = 0;
        std::uint32_t combatRevision = 0;
        std::uint8_t epoch           = 0;
        bool valid                   = true;

        /**
         * The request the vault was asked with: the jump key, or the crouch
         * key a type-9 ledge takes instead. The receiver's copy is a
         * player-controlled C_Player and the engine refuses its vault
         * without the same request.
         */
        bool jumpRequested   = false;
        bool crouchRequested = false;

        /**
         * When the server relayed this, on the server's clock; zero on the
         * way up. A receiver shifts it onto its own clock -- the one the
         * actor's relayed poses are stamped on -- and plays it once the body
         * is drawn at that moment, rather than ahead of the pose.
         */
        MafiaNet::Time relayedAt = 0;

        bool IsSane() const {
            return valid && presentationSequence != 0 && incarnation != 0 && combatRevision != 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, presentationSequence) && bs->Serialize(write, climberNetworkId) && bs->Serialize(write, incarnation) && bs->Serialize(write, combatRevision) && bs->Serialize(write, epoch) && bs->Serialize(write, relayedAt) && bs->Serialize(write, jumpRequested) && bs->Serialize(write, crouchRequested);
        }
    };
} // namespace KCDC::Shared::RPC
