#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/siege/siege_ballistics.h"
#include "shared/features/siege/siege_wire.h"
#include "shared/game/siege_catalog.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * Server -> client: a siege engine has let its projectile go.
     *
     * Everything the flight is: where it left, how fast, and the server clock
     * it left on. Every receiver draws the stone along that arc from its own
     * clock and looks for what it hits; none of them simulates anything.
     *
     * Sent to the players near the engine and near where the stone is aimed,
     * as a burst of particles is. `report` is set on exactly one of them --
     * the one whose world the server trusts to say where the stone came down,
     * because the server has no geometry of its own to trace it against.
     */
    struct SiegeShot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeShot");

        std::uint32_t shotId = 0;

        /** The engine that threw it, so a receiver's trace passes through the frame the stone leaves from. */
        std::uint64_t engineNetworkId = 0;
        std::uint8_t assetIndex       = 0;
        glm::vec3 origin {0.0F};
        glm::vec3 velocity {0.0F};
        std::uint64_t launchMs = 0;
        bool report            = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(shotId);
            fields.Field(engineNetworkId);
            fields.Field(assetIndex);
            SiegeWire::Vector(fields, origin);
            SiegeWire::Vector(fields, velocity);
            fields.Field(launchMs);
            fields.Field(report);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && shotId != 0 && Shared::Game::SiegeEngineAssetAt(assetIndex) != nullptr && SiegeWire::InWorld(origin) && Shared::Game::IsFinite(velocity) && glm::length(velocity) < 1000.0F;
        }

      private:
        bool _complete = true;
    };

    /**
     * Reporting client -> server: the stone of `shotId` came down here.
     *
     * Only the client the shot named may send it, and the server accepts it
     * only near the arc it solved. A shot nobody reports lands where it was
     * aimed once its flight time has run out.
     */
    struct SiegeImpactReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeImpactReport");

        std::uint32_t shotId = 0;
        glm::vec3 point {0.0F};

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(shotId);
            SiegeWire::Vector(fields, point);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && shotId != 0 && SiegeWire::InWorld(point);
        }

      private:
        bool _complete = true;
    };

    /**
     * What a player asked of an engine: to take its place (`Operate`) or a
     * crew place (`Winch`), to step away from either (`Leave`), or -- from
     * the operator's place -- to load it or let fly at its aim.
     */
    enum class SiegeUseAction : std::uint8_t { Load, Fire, Operate, Leave, Winch };

    inline constexpr std::uint8_t kLastSiegeUseAction = static_cast<std::uint8_t>(SiegeUseAction::Winch);

    /**
     * Client -> server: the local player pressed an engine's native prompt,
     * or one of the operator's keys.
     *
     * Carries what was asked, not just that a key went down: the engine may
     * have changed step between the client offering it and the server
     * reading this, and a Fire pressed on an engine that has since fired must
     * not load it again. The server checks the player stands at the engine,
     * that it is usable, and that only its operator works it.
     */
    struct SiegeUse {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeUse");

        std::uint64_t engineNetworkId = 0;
        SiegeUseAction action         = SiegeUseAction::Load;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(engineNetworkId);
            std::uint8_t wireAction = static_cast<std::uint8_t>(action);
            fields.Field(wireAction);
            _validAction = wireAction <= kLastSiegeUseAction;
            action       = _validAction ? static_cast<SiegeUseAction>(wireAction) : SiegeUseAction::Load;
            _complete    = fields.Good();
        }

        bool IsSane() const {
            return _complete && _validAction && engineNetworkId != 0;
        }

      private:
        bool _complete    = true;
        bool _validAction = true;
    };

    /**
     * Operator client -> server: where the operator is looking, to lay the
     * engine on.
     *
     * Sent while the aim key is held, only when the point has moved enough
     * to matter and never faster than a few times a second. The server lays
     * the engine there if it can reach it -- turning it, and setting
     * `aimTarget` for every client to solve the same launch from -- and
     * otherwise leaves the old aim alone.
     */
    struct SiegeAim {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeAim");

        std::uint64_t engineNetworkId = 0;
        glm::vec3 target {0.0F};

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(engineNetworkId);
            SiegeWire::Vector(fields, target);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && engineNetworkId != 0 && SiegeWire::InWorld(target);
        }

      private:
        bool _complete = true;
    };

    /**
     * Server -> client: where a stone actually came down.
     *
     * Every receiver ends that shot's flight here if it is still drawing it,
     * plays the impact unless it already has nearby, and takes whatever the
     * blast does to its own player. A client only ever hurts its own body --
     * the same rule every other hit in the mod follows -- so the damage
     * arrives through the game's own pipeline and is reported like a blow.
     */
    struct SiegeImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SiegeImpact");

        std::uint32_t shotId = 0;

        /** The player credited with the blast, whose soul a client hurts its own body in the name of; 0 for nobody. */
        std::uint64_t attackerNetworkId = 0;
        std::uint8_t assetIndex         = 0;
        glm::vec3 point {0.0F};
        float damage       = 0.0F;
        float damageRadius = 0.0F;

        /** Whether it struck a siege engine, which throws up its debris as well. */
        bool struckEngine = false;

        static constexpr float kMaxDamage       = 10000.0F;
        static constexpr float kMaxDamageRadius = 100.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(shotId);
            fields.Field(attackerNetworkId);
            fields.Field(assetIndex);
            SiegeWire::Vector(fields, point);
            fields.Field(damage);
            fields.Field(damageRadius);
            fields.Field(struckEngine);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && shotId != 0 && Shared::Game::SiegeEngineAssetAt(assetIndex) != nullptr && SiegeWire::InWorld(point) && std::isfinite(damage) && damage >= 0.0F && damage <= kMaxDamage && std::isfinite(damageRadius) && damageRadius >= 0.0F
                   && damageRadius <= kMaxDamageRadius;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
