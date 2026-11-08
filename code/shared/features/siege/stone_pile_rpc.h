#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/siege/siege_wire.h"
#include "shared/features/siege/stone_pile_entity.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * What the local player did at a pile, as the game's own stone throwing
     * did it: grabbed a stone (`Grab`), threw the stone in the hands on the
     * throw key (`Throw`) or stepped away from the pile (`Leave`).
     */
    enum class StonePileAction : std::uint8_t { Grab, Leave, Throw };

    inline constexpr std::uint8_t kLastStonePileAction = static_cast<std::uint8_t>(StonePileAction::Throw);

    /**
     * Client -> server: the local player's stone throwing started a grab at
     * the pile, or finished.
     *
     * Reported after the fact, because the game only offers the grab while
     * the pile shows a stone -- which it shows only while the server's count
     * allows -- and the grab cannot be taken back once the fragment runs. The
     * server takes the stone off its count and has every other client replay
     * it on the player's body.
     */
    struct StonePileUse {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StonePileUse");

        std::uint64_t pileNetworkId = 0;
        StonePileAction action      = StonePileAction::Grab;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(pileNetworkId);
            std::uint8_t wireAction = static_cast<std::uint8_t>(action);
            fields.Field(wireAction);
            _validAction = wireAction <= kLastStonePileAction;
            action       = _validAction ? static_cast<StonePileAction>(wireAction) : StonePileAction::Grab;
            _complete    = fields.Good();
        }

        bool IsSane() const {
            return _complete && _validAction && pileNetworkId != 0;
        }

      private:
        bool _complete    = true;
        bool _validAction = true;
    };

    /**
     * Server -> every other client near the pile: `playerNetworkId` grabbed
     * or left it. The receiver runs the same native stone throwing on that
     * player's body, so the grab, the throw and the stone are the game's.
     */
    struct StonePileUsed {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StonePileUsed");

        std::uint64_t pileNetworkId   = 0;
        std::uint64_t playerNetworkId = 0;
        StonePileAction action        = StonePileAction::Grab;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(pileNetworkId);
            fields.Field(playerNetworkId);
            std::uint8_t wireAction = static_cast<std::uint8_t>(action);
            fields.Field(wireAction);
            _validAction = wireAction <= kLastStonePileAction;
            action       = _validAction ? static_cast<StonePileAction>(wireAction) : StonePileAction::Grab;
            _complete    = fields.Good();
        }

        bool IsSane() const {
            return _complete && _validAction && pileNetworkId != 0 && playerNetworkId != 0;
        }

      private:
        bool _complete    = true;
        bool _validAction = true;
    };

    /**
     * Thrower's client -> server: the stone it threw from `pileNetworkId`
     * first met something here. Only the thrower's own game knows where its
     * stone went; the server checks the point is within a stone's throw of
     * the pile before hurting anyone with it.
     */
    struct StoneImpactReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StoneImpactReport");

        std::uint64_t pileNetworkId = 0;
        glm::vec3 point {0.0F};

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(pileNetworkId);
            SiegeWire::Vector(fields, point);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && pileNetworkId != 0 && SiegeWire::InWorld(point);
        }

      private:
        bool _complete = true;
    };

    /**
     * Server -> clients near the point: a thrown stone came down. Each client
     * takes its own player's share through the game's damage, as a siege
     * engine's blast is taken; NPCs have already taken theirs on the server.
     */
    struct StoneImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StoneImpact");

        std::uint64_t attackerNetworkId = 0;
        glm::vec3 point {0.0F};
        float damage       = 0.0F;
        float damageRadius = 0.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(attackerNetworkId);
            SiegeWire::Vector(fields, point);
            fields.Field(damage);
            fields.Field(damageRadius);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && SiegeWire::InWorld(point) && std::isfinite(damage) && damage >= 0.0F && damage <= Shared::Entities::StonePileEntity::kMaxDamage && std::isfinite(damageRadius) && damageRadius >= 0.0F && damageRadius <= Shared::Entities::StonePileEntity::kMaxDamageRadius;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
