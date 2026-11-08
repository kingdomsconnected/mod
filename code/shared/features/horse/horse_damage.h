#pragma once

#include "shared/features/player/player_damage.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A blow landed on a horse somewhere other than the horse's simulator.
     *
     * Attacker -> server -> simulator. The attacker's own game resolved the
     * hit against its copy of the animal -- the swing, the arrow, the armour
     * and the tables are all there -- but that copy's soul is not the horse's:
     * only the simulator's is. So the attacker's copy drops the write and
     * sends what it would have taken, and the server hands it to the one peer
     * whose soul counts. The same division PvP runs on, with the victim's own
     * machine applying the damage.
     *
     * `attackerNetworkId` is stamped by the server from the sending
     * connection on the way through; whatever a client puts there is ignored.
     */
    struct HorseHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseHit");

        std::uint64_t horseNetworkId    = 0;
        std::uint64_t attackerNetworkId = 0;

        /** Health the attacker's own calculation took off. */
        float amount = 0.0F;

        /** A `Game::DamageReason`. */
        std::uint8_t reason = 0;

        /** Bounds what one message may claim; a horse's health tops out far below. */
        static constexpr float kMaxAmount = 100000.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(horseNetworkId);
            fields.Field(attackerNetworkId);
            fields.Field(amount);
            fields.Field(reason);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && horseNetworkId != 0 && std::isfinite(amount) && amount > 0.0F && amount <= kMaxAmount && reason <= Game::kMaxDamageReason;
        }

      private:
        bool _complete = true;
    };

    /**
     * Simulator -> server: the horse lost health, and to whom.
     *
     * What `horseDamage` is raised from. The health itself travels on the
     * entity's field channel; this is the event beside it, with the attacker
     * the simulator's engine named and the amount it really took. A killing
     * blow says so, which is what `horseDeath` is raised from.
     */
    struct HorseDamaged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseDamaged");

        std::uint64_t horseNetworkId = 0;

        /** The player whose blow it was, as the simulator saw it; 0 for nobody. */
        std::uint64_t attackerNetworkId = 0;

        float amount       = 0.0F;
        std::uint8_t reason = 0;

        /** This write took the last of the health. */
        bool fatal = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(horseNetworkId);
            fields.Field(attackerNetworkId);
            fields.Field(amount);
            fields.Field(reason);
            fields.Field(fatal);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && horseNetworkId != 0 && std::isfinite(amount) && amount > 0.0F && amount <= HorseHit::kMaxAmount && reason <= Game::kMaxDamageReason;
        }

      private:
        bool _complete = true;
    };

    /**
     * Simulator -> server: the horse's soul died, from the engine's own death
     * dispatch.
     *
     * Sent on the same ordered channel as `HorseDamaged`, after the report of
     * the blow that caused it, so `horseDeath` always follows its killing
     * `horseDamage`. The replicated `alive` still tells every observer; this
     * is the edge the server announces from, rather than a flag it would have
     * to poll for.
     */
    struct HorseDied {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseDied");

        std::uint64_t horseNetworkId = 0;

        /** The player whose blow took the last of the health, as the simulator saw it; 0 for nobody. */
        std::uint64_t killerNetworkId = 0;

        /** A `Game::DamageReason`. */
        std::uint8_t reason = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(horseNetworkId);
            fields.Field(killerNetworkId);
            fields.Field(reason);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && horseNetworkId != 0 && reason <= Game::kMaxDamageReason;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
