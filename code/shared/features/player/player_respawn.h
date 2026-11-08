#pragma once

#include "shared/features/player/player_damage.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /** Server permission to revive the identified life of a player. */
    struct RespawnPlayer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::RespawnPlayer");

        std::uint64_t networkId     = 0;
        std::uint32_t incarnation   = 0;
        std::uint32_t deathRevision = 0;
        std::uint8_t epoch          = 0;
        bool valid                 = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, networkId) && bs->Serialize(write, incarnation) && bs->Serialize(write, deathRevision) && bs->Serialize(write, epoch);
        }

        bool IsSane() const {
            return valid && networkId != 0 && incarnation != 0 && deathRevision != 0;
        }
    };

    /**
     * Owner -> server: this life ended. The permission a revival will need,
     * plus what the dying client saw kill it.
     */
    struct PlayerDied: RespawnPlayer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerDied");

        /** The player whose write took the last of the health, as this client saw it; 0 for nobody. */
        std::uint64_t killerNetworkId = 0;

        /** A `Game::DamageReason`: the reason on that fatal write. */
        std::uint8_t reason = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            RespawnPlayer::Serialize(bs, write);
            valid = valid && bs->Serialize(write, killerNetworkId) && bs->Serialize(write, reason);
        }

        bool IsSane() const {
            return RespawnPlayer::IsSane() && reason <= Game::kMaxDamageReason;
        }
    };
} // namespace KCDC::Shared::RPC
