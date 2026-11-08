#pragma once

#include "shared/features/player/player_buff.h"
#include "shared/game/buff_catalog.h"

#include <mafianet/types.h>

#include <cstdint>
#include <map>
#include <vector>

namespace KCDC::Shared::Entities {
    class HumanEntity;
} // namespace KCDC::Shared::Entities

namespace KCDC::Features::Player {
    /**
     * Server-decided status effects, and what each body actually wears.
     *
     * The server holds no buff of its own, so every verb here is an
     * instruction to one owning client and everything read back is that
     * client's own report. Timers run on the owner's frame clock, so this
     * module never expires anything.
     *
     * Orders are not tracked for delivery -- they travel `ReliableOrdered`.
     * What is tracked is which removals this server asked for, which is all
     * that separates an ordered removal from an expiry.
     */
    class Buffs final {
      public:
        void Install();
        void Update();
        void PublishDefinition(const std::string &id);

        /** Hands the new connection the current claim, so its gate is armed on arrival. */
        void OnPlayerConnect(MafiaNet::PeerGuid guid);
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid, std::uint64_t playerNetworkId);

        std::uint32_t ClaimedClasses() const {
            return _claimedClasses;
        }

        /**
         * Takes ownership of whole buff classes: every client then refuses a
         * locally originated add in one and reports the refusal. Nothing is
         * claimed by default, so an unclaimed class behaves as it does in
         * single player.
         */
        void ClaimClasses(std::uint32_t mask);

        /** False when there is no connection to ask. Nonzero incarnation binds delivery to that body. */
        bool Apply(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff, std::uint32_t incarnation = 0);

        /** By handle when a report named one, by definition otherwise. */
        bool Retract(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff);

        bool ClearByTag(Shared::Entities::HumanEntity *player, int aiTagId);

        /** Null for a player who has reported nothing yet. Each row carries when this server first saw that buff. */
        const std::vector<Shared::RPC::ObservedBuff> *Report(std::uint64_t playerNetworkId) const;

        bool Has(std::uint64_t playerNetworkId, const Shared::RPC::BuffId &buff) const;

      private:
        struct PlayerBuffs {
            MafiaNet::PeerGuid owner = MafiaNet::PeerGuid();
            std::uint32_t revision   = 0;
            std::vector<Shared::RPC::ObservedBuff> active;

            /** Definitions this server asked to have taken off, until they are gone. */
            std::vector<Shared::RPC::BuffId> pendingRemovals;
            std::map<std::uint64_t, std::uint64_t> nextTicks;
        };

        void AcceptReport(std::uint64_t playerNetworkId, MafiaNet::PeerGuid guid, const Shared::RPC::BuffReport &report);
        PlayerBuffs *FindPlayer(std::uint64_t playerNetworkId);
        const PlayerBuffs *FindPlayer(std::uint64_t playerNetworkId) const;

        std::map<std::uint64_t, PlayerBuffs> _players;
        std::uint32_t _claimedClasses = 0;
    };
} // namespace KCDC::Features::Player
