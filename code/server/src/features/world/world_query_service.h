#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/world/world_query_rpc.h"

#include <mafianet/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::World {
    /**
     * The server's way of asking about a world it does not have: it borrows a
     * named player's client, as `DoorService::QueryNearest` does. The answer
     * is that client's view, a round trip late, and its word -- the script
     * contract says so rather than hiding it.
     *
     * Every handler runs exactly once, on the answer, the timeout or a
     * disconnect, so a promise built on one always settles.
     */
    class WorldQueryService final {
      public:
        /** What came back, or why nothing did. */
        struct Answer {
            bool answered = false;
            std::string reason;
            KCDC::Shared::RPC::WorldQueryKind kind = KCDC::Shared::RPC::WorldQueryKind::Raycast;
            std::vector<KCDC::Shared::RPC::WorldRayHit> hits;
            std::vector<KCDC::Shared::RPC::WorldNearbyEntity> entities;
        };

        /** Runs on the server tick, once per query. */
        using AnswerHandler = std::function<void(const Answer &)>;

        void Install(const std::string &level);
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * `query.requestId` is filled in here; the rest must already have
         * passed `IsSane`. False when there is no peer to ask, and then
         * `handler` does not run at all.
         */
        bool Ask(KCDC::Shared::Entities::HumanEntity &caller, KCDC::Shared::RPC::WorldQuery query, AnswerHandler handler);

        std::size_t PendingCount() const {
            return _pending.size();
        }

      private:
        struct Pending {
            MafiaNet::PeerGuid sender              = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t expiresAt                = 0;
            KCDC::Shared::RPC::WorldQueryKind kind = KCDC::Shared::RPC::WorldQueryKind::Raycast;
            AnswerHandler handler;
        };

        void QueueAnswer(const KCDC::Shared::RPC::WorldQueryAnswer &answer, MafiaNet::Packet *packet);
        void QueueWeaponHit(const KCDC::Shared::RPC::WeaponHitReport &report, MafiaNet::Packet *packet);

        /** Raises `playerWeaponHit` for a report from a player in this level, near what they say they hit, inside their budget. */
        void ResolveWeaponHit(const KCDC::Shared::RPC::WeaponHitReport &report, MafiaNet::PeerGuid sender);
        void ResolveAnswer(const KCDC::Shared::RPC::WorldQueryAnswer &answer, MafiaNet::PeerGuid sender);

        /** Drops the query as it settles, so its handler cannot run twice. */
        void Settle(std::uint64_t requestId, Answer answer);

        std::mutex _incomingMutex;
        std::vector<std::pair<KCDC::Shared::RPC::WorldQueryAnswer, MafiaNet::PeerGuid>> _incomingAnswers;
        std::unordered_map<std::uint64_t, Pending> _pending;

        std::vector<std::pair<KCDC::Shared::RPC::WeaponHitReport, MafiaNet::PeerGuid>> _incomingWeaponHits;

        // Each player's reports in the current second, against `kMaxWeaponHitsPerSecond`.
        struct WeaponHitBudget {
            std::uint64_t windowStart = 0;
            std::uint32_t count       = 0;
        };

        std::unordered_map<MafiaNet::PeerGuid, WeaponHitBudget> _weaponHitBudgets;
        std::uint64_t _nextRequestId = 1;
        std::string _level;
        std::uint32_t _levelHash = 0;
    };
} // namespace KCDC::Features::World
