#pragma once

#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/player/item_carry_policy.h"
#include "shared/features/player/player_carry.h"
#include "shared/features/player/player_damage.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_respawn.h"
#include "shared/features/player/player_spawn.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <mafianet/types.h>

#include <unordered_map>
#include <unordered_set>

namespace KCDC::Features::Player {
    class PlayerService final {
      public:
        static void Install();
        void UpdateStats();
        bool Revive(KCDC::Shared::Entities::HumanEntity *human);
        void OnPlayerDisconnect(MafiaNet::PeerGuid owner);

        // The spawn barrier released for this connection: `playerSpawned`
        // has been raised, and `playerReady` may follow.
        void OnPlayerSpawned(MafiaNet::PeerGuid owner);

        // From inside that player's own playerSpawning this names the answer
        // to their outstanding ask; anywhere else it goes out immediately.
        bool Place(KCDC::Shared::Entities::HumanEntity *player, const glm::vec3 &position, const glm::quat &rotation);

        // Orders a player's own client to carry an item: made straight in their hands, or taken up from `groundItem` with the game's pick-up. A carry the server ordered is not put to `playerCarryingItem` when it reports in -- the order was the decision. False when the player may not carry (`ItemCarryRefusal`), the stack is not theirs to take (`GroundItemService::CarryRefusal`), or there is no connection to ask.
        bool OrderItemCarry(KCDC::Shared::Entities::HumanEntity *player, const KCDC::Shared::RPC::ItemClassId &itemClass, KCDC::Shared::Entities::GroundItemEntity *groundItem);

        // Ends the item carry `carrier` is recorded as holding: clears the record everyone else replays, puts the item down as a carryable prop when it went to the ground, and raises `playerPutDownItem`.
        void EndItemCarry(KCDC::Shared::Entities::HumanEntity *carrier, bool dropped, const KCDC::Shared::RPC::PlayerCarryItem *report);

      private:
        struct StatSnapshot {
            std::uint32_t incarnation = 0;
            std::array<float, Shared::Game::kPlayerStats.size() + Shared::Game::kDerivedPlayerStats.size()> values {};
        };
        std::unordered_map<std::uint64_t, StatSnapshot> _statSnapshots;
        struct Death {
            KCDC::Shared::RPC::RespawnPlayer permission;
            bool reviveRequested = false;
        };

        struct Choice {
            glm::vec3 position {0.0F, 0.0F, 0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            bool chosen = false;
        };

        void ReportDeath(const KCDC::Shared::RPC::PlayerDied &report, MafiaNet::PeerGuid owner);

        // The victim is the sender's own body; the attacker is its claim,
        // kept only when it names another connected player.
        void ReportDamage(const KCDC::Shared::RPC::PlayerDamaged &report, MafiaNet::PeerGuid owner);

        // A carrier's own client picked a body up or let one go. The carrier is the sending connection; only the body comes from the message.
        void ReportCarry(const KCDC::Shared::RPC::PlayerCarry &report, MafiaNet::PeerGuid owner);

        // Ends the carry `carrier` is recorded as holding: clears the record everyone else replays and raises `playerPutDown`. The carrier's client is told to let go too when `orderClient` -- when its own game is not the one that ended it.
        static void EndCarry(KCDC::Shared::Entities::HumanEntity *carrier, bool orderClient);

        // A carrier's own client took an item into their arms or stopped carrying one. The carrier is the sending connection.
        void ReportItemCarry(const KCDC::Shared::RPC::PlayerCarryItem &report, MafiaNet::PeerGuid owner);

        // A player chose the game's pick-up prompt over a carryable prop: asked of `playerCarryingItem`, then ordered like a scripted carry.
        void RequestGroundItemCarry(const KCDC::Shared::RPC::RequestCarryGroundItem &request, MafiaNet::PeerGuid owner);

        // Answers a carry its client already started with a refusal: let go at once, and an item taken up from a stack goes back to it.
        void RefuseItemCarry(KCDC::Shared::Entities::HumanEntity *carrier, std::uint64_t groundItemNetworkId);

        // What `ItemCarryRefusal` judges this player by.
        static KCDC::Shared::Game::ItemCarryState CarryStateOf(KCDC::Shared::Entities::HumanEntity &player);

        /** A carry the server ordered and has not seen reported yet, by the carrier's network id. */
        struct ItemCarryOrder {
            KCDC::Shared::RPC::ItemClassId itemClass;
            std::uint64_t groundItemNetworkId = 0;
            std::uint64_t expiresAt           = 0;
        };

        // The asking client holds its join barrier shut until an answer
        // arrives, so every path out of this sends one.
        void ResolveSpawn(const KCDC::Shared::RPC::RequestSpawn &request, MafiaNet::PeerGuid owner);

        // Once per connection, and never ahead of `playerSpawned`: the order
        // is the one scripts are promised, whatever a client sends.
        void ReportReady(MafiaNet::PeerGuid owner);

        std::unordered_map<MafiaNet::PeerGuid, Death> _deaths;

        std::unordered_map<std::uint64_t, ItemCarryOrder> _itemCarryOrders;

        // Connections past the spawn barrier that have not been reported
        // ready yet.
        std::unordered_set<MafiaNet::PeerGuid> _awaitingReady;

        // The ask currently being answered, if one is.
        MafiaNet::PeerGuid _resolving = MafiaNet::UNASSIGNED_PEER_GUID;
        Choice _choice;
        bool _resolvingActive = false;
    };
} // namespace KCDC::Features::Player
