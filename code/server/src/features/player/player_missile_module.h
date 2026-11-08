#pragma once

#include "shared/features/player/player_missile.h"
#include "shared/features/player/player_missile_shot_record.h"
#include <mafianet/types.h>

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

namespace KCDC::Features::Player {
    class Missile final {
      public:
        void Install();
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * Pays out the damage an accepted impact owes its target, once: true
         * when shot `sequence` of `shooterNetworkId` was accepted onto
         * `victimNetworkId` and nothing has claimed it yet. The combat referee
         * asks before it rules on the victim's report of the arrow.
         */
        bool ClaimDamage(std::uint64_t shooterNetworkId, std::uint64_t sequence, std::uint64_t victimNetworkId, std::uint64_t now);

        /** How long an accepted impact keeps its damage owed: the shot's own lifetime, for the victim's client to apply it, and the report's trip back. */
        static constexpr std::uint64_t kDamageClaimMs = Shared::RPC::MissileLaunched::kLifetimeMs + 2000;

      private:
        struct PlayerShots {
            MafiaNet::PeerGuid guid    = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t lastSequence = 0;
            std::uint64_t refillAt     = 0;
            double tokens              = 64.0;
            std::unordered_map<std::uint64_t, Shared::Game::MissileShotRecord> shots;
        };

        void OnLaunched(const Shared::RPC::MissileLaunched &message, MafiaNet::Packet *packet);
        void OnEnded(const Shared::RPC::MissileEnded &message, MafiaNet::Packet *packet);
        void OnImpact(const Shared::RPC::MissileImpact &message, MafiaNet::Packet *packet);
        void OnRetained(const Shared::RPC::MissileRetained &message, MafiaNet::Packet *packet);

        std::unordered_map<std::uint64_t, PlayerShots> _players;

        /** An accepted impact's damage, not yet claimed. Kept apart from the shot, which its end erases. */
        struct OwedDamage {
            std::uint64_t victimNetworkId = 0;
            std::uint64_t expiresAt       = 0;
        };

        // Keyed by shooter NetworkID and shot sequence.
        std::map<std::pair<std::uint64_t, std::uint64_t>, OwedDamage> _owedDamage;
    };
} // namespace KCDC::Features::Player
