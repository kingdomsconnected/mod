#include "features/player/player_service.h"

#include "core/server.h"
#include "features/ground_item/ground_item_scripting.h"
#include "features/ground_item/ground_item_service.h"
#include "features/ground_item/ground_stock.h"
#include "features/horse/horse_verbs.h"
#include "features/inventory/consumption_scripting.h"
#include "features/player/player_combatant.h"
#include "features/player/player_roster.h"
#include "features/player/player_scripting.h"
#include "shared/features/player/player_death.h"
#include "shared/features/player/player_ready.h"
#include "shared/game/animation_catalog.h"
#include "shared/game/body_part_catalog.h"
#include "shared/game/item_classes.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <fmt/format.h>
#include <mafianet/get_time.h>

#include <cmath>
#include <utility>

namespace KCDC::Features::Player {
    void PlayerService::UpdateStats() {
        struct Change {
            std::uint64_t player;
            std::uint32_t incarnation;
            nlohmann::json values;
        };
        std::vector<Change> pending;
        Framework::CoreModules::GetReplication()->ForEach<Shared::Entities::HumanEntity>([&](auto *player) {
            const auto id = player->GetNetworkID();
            if (!player->soul.valid) {
                _statSnapshots.erase(id);
                return;
            }
            StatSnapshot current;
            current.incarnation = player->soul.incarnation;
            for (std::size_t i = 0; i < Shared::Game::kPlayerStats.size(); ++i) {
                current.values[i] = player->soul.resources[i].current;
            }
            for (std::size_t i = 0; i < Shared::Game::kDerivedPlayerStats.size(); ++i) {
                current.values[Shared::Game::kPlayerStats.size() + i] = i < Shared::Game::kPlayerConditionCount ? player->soul.conditions[i] : player->statReadings.values[i - Shared::Game::kPlayerConditionCount];
            }
            const auto previous    = _statSnapshots.find(id);
            nlohmann::json changes = nlohmann::json::array();
            if (previous != _statSnapshots.end() && previous->second.incarnation == current.incarnation) {
                for (std::size_t i = 0; i < current.values.size(); ++i) {
                    if (current.values[i] != previous->second.values[i]) {
                        const auto name = i < Shared::Game::kPlayerStats.size() ? Shared::Game::kPlayerStats[i].value : Shared::Game::kDerivedPlayerStats[i - Shared::Game::kPlayerStats.size()].value;
                        changes.push_back({{"stat", name}, {"previous", previous->second.values[i]}, {"current", current.values[i]}});
                    }
                }
            }
            _statSnapshots.insert_or_assign(id, current);
            if (!changes.empty()) {
                pending.push_back({id, current.incarnation, std::move(changes)});
            }
        });
        // Handlers may disconnect players; never emit while iterating replicas.
        for (const auto &change : pending) {
            const auto *player = Players::Find(change.player);
            if (player != nullptr && player->soul.valid && player->soul.incarnation == change.incarnation) {
                KCDC::Scripting::PlayerStatsChanged(change.player, change.values);
            }
        }
    }

    namespace {
        constexpr const char *kLogger = "Player";

        constexpr float kWorldLimit = 100000.0F;

        // How far apart a carrier and the body they picked up may be, as
        // their owners last reported them. The grab itself is an arm's
        // length; this allows for a corpse that ragdolled a little further
        // on its own machine than on the carrier's.
        constexpr float kMaxCarryDistance = 10.0F;

        // How long an ordered item carry waits for its client to report it.
        constexpr MafiaNet::Time kItemCarryOrderTimeoutMs = 5000;

        bool IsInWorld(const glm::vec3 &value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::abs(value.x) < kWorldLimit && std::abs(value.y) < kWorldLimit && std::abs(value.z) < kWorldLimit;
        }

        glm::quat NormalizedOrIdentity(const glm::quat &rotation) {
            const float lengthSquared = rotation.w * rotation.w + rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z;
            if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6F) {
                return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            }
            return glm::normalize(rotation);
        }

        /**
         * A player a victim's client named as the one who hurt it, or 0.
         *
         * The claim is the victim's own and is kept only as far as it names a
         * connected player who is not the victim: a client can blame somebody
         * for its own injuries, but not invent a body or blame itself.
         */
        std::uint64_t VerifiedAttacker(std::uint64_t claimedNetworkId, KCDC::Shared::Entities::HumanEntity &victim) {
            if (claimedNetworkId == 0 || claimedNetworkId == victim.GetNetworkID()) {
                return 0;
            }
            return Combatant::Find(claimedNetworkId) ? claimedNetworkId : 0;
        }

        bool Send(KCDC::Shared::Entities::HumanEntity *player, const KCDC::Shared::RPC::SpawnPlayer &spawn) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (player == nullptr || net == nullptr) {
                return false;
            }
            // The receiver drops anything failing its own check.
            if (!spawn.IsSane()) {
                return false;
            }
            KCDC::Shared::RPC::SpawnPlayer payload = spawn;
            net->SendRPC(payload, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            return true;
        }
    } // namespace

    void PlayerService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        net->RegisterRPC<KCDC::Shared::RPC::PlayerDied>([](const KCDC::Shared::RPC::PlayerDied &report, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (report.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().ReportDeath(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::PlayerDamaged>([](const KCDC::Shared::RPC::PlayerDamaged &report, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (report.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().ReportDamage(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::RequestSpawn>([](const KCDC::Shared::RPC::RequestSpawn &request, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (request.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().ResolveSpawn(request, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::PlayerCarry>([](const KCDC::Shared::RPC::PlayerCarry &report, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (report.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().ReportCarry(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::PlayerCarryItem>([](const KCDC::Shared::RPC::PlayerCarryItem &report, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (report.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().ReportItemCarry(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::RequestCarryGroundItem>([](const KCDC::Shared::RPC::RequestCarryGroundItem &request, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (request.IsSane() && packet != nullptr && server != nullptr) {
                server->GetPlayers().RequestGroundItemCarry(request, MafiaNet::ToPeerGuid(packet->guid));
            }
        });

        net->RegisterRPC<KCDC::Shared::RPC::PlayerReady>([](const KCDC::Shared::RPC::PlayerReady &, MafiaNet::Packet *packet) {
            auto *server = KCDC::Server::Get();
            if (packet != nullptr && server != nullptr) {
                server->GetPlayers().ReportReady(MafiaNet::ToPeerGuid(packet->guid));
            }
        });
    }

    void PlayerService::OnPlayerSpawned(MafiaNet::PeerGuid owner) {
        _awaitingReady.insert(owner);
    }

    void PlayerService::ReportReady(MafiaNet::PeerGuid owner) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (_awaitingReady.erase(owner) == 0 || human == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Ignoring a ready report from peer {}", static_cast<std::uint64_t>(owner));
            return;
        }
        KCDC::Scripting::Events::PlayerReady(human->GetNetworkID());
    }

    void PlayerService::ReportDeath(const KCDC::Shared::RPC::PlayerDied &report, MafiaNet::PeerGuid owner) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (human == nullptr || human->GetNetworkID() != report.networkId || human->stateEpoch != report.epoch) {
            return;
        }
        auto &death = _deaths[owner];
        if (!KCDC::Shared::Game::AcceptsDeathReport(report, death.permission)) {
            return;
        }
        death.permission     = report;
        death.reviveRequested = false;

        // Nothing native throws a dead rider. Their own client has already
        // ended the ride from the soul's death edge, so it is not ordered off
        // again; this makes the server's record agree before any handler runs,
        // so a respawn never drags the horse along with the body.
        KCDC::Features::Horse::Horses::TakeRiderOff(KCDC::Features::Horse::Horses::RiddenBy(report.networkId), false);
        KCDC::Scripting::Events::PlayerDied(report.networkId, VerifiedAttacker(report.killerNetworkId, *human), KCDC::Shared::Game::ToDamageReason(report.reason));
    }

    void PlayerService::ReportDamage(const KCDC::Shared::RPC::PlayerDamaged &report, MafiaNet::PeerGuid owner) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (human == nullptr) {
            return;
        }
        // Only a limb a script can name; an animal's part never reaches a player.
        const std::uint8_t bodyPart = KCDC::Shared::Game::FindHumanLimb(report.bodyPart) != nullptr ? report.bodyPart : KCDC::Shared::Game::kNoBodyPart;
        KCDC::Scripting::Events::PlayerDamaged(human->GetNetworkID(), VerifiedAttacker(report.attackerNetworkId, *human), report.amount, bodyPart, KCDC::Shared::Game::ToDamageReason(report.reason));
    }

    bool PlayerService::Revive(KCDC::Shared::Entities::HumanEntity *human) {
        if (human == nullptr) {
            return false;
        }
        const auto it = _deaths.find(human->ownerGUID);
        if (it == _deaths.end() || it->second.reviveRequested || it->second.permission.networkId != human->GetNetworkID() || it->second.permission.epoch != human->stateEpoch) {
            return false;
        }
        it->second.reviveRequested = true;

        // Nobody carries the living. Every client lets go of this body as it
        // revives it; the carrier's is told as well, so the put-down is the
        // server's rather than a report racing the revival.
        EndCarry(Players::CarrierOf(human->GetNetworkID()), true);

        Framework::CoreModules::GetNetworkPeer()->SendRPC(it->second.permission, MafiaNet::ToGuid(human->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool PlayerService::Place(KCDC::Shared::Entities::HumanEntity *player, const glm::vec3 &position, const glm::quat &rotation) {
        if (player == nullptr || !IsInWorld(position)) {
            return false;
        }

        const glm::quat facing = NormalizedOrIdentity(rotation);

        // Inside this player's own playerSpawning: name the answer rather
        // than race it with a second placement.
        if (_resolvingActive && _resolving == player->ownerGUID) {
            _choice.position = position;
            _choice.rotation = facing;
            _choice.chosen   = true;
            return true;
        }

        KCDC::Shared::RPC::SpawnPlayer spawn;
        spawn.x         = position.x;
        spawn.y         = position.y;
        spawn.z         = position.z;
        spawn.rotationX = facing.x;
        spawn.rotationY = facing.y;
        spawn.rotationZ = facing.z;
        spawn.rotationW = facing.w;
        spawn.placed    = true;
        return Send(player, spawn);
    }

    void PlayerService::ResolveSpawn(const KCDC::Shared::RPC::RequestSpawn &request, MafiaNet::PeerGuid owner) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        // A peer asking about a body that is not the one it owns.
        if (human == nullptr || human->GetNetworkID() != request.networkId) {
            Framework::Logging::GetLogger(kLogger)->warn("Ignoring a spawn request from peer {} for body {}", static_cast<std::uint64_t>(owner), request.networkId);
            return;
        }

        // Resource code can throw, and the flag below is what tells Place()
        // an ask is being answered: left standing it would hang every join.
        {
            struct DispatchScope final {
                PlayerService &owner;
                ~DispatchScope() {
                    owner._resolvingActive = false;
                    owner._resolving       = MafiaNet::UNASSIGNED_PEER_GUID;
                }
            } scope {*this};

            _resolving       = owner;
            _choice          = {};
            _resolvingActive = true;
            KCDC::Scripting::Events::PlayerSpawning(request.networkId);
        }

        // A handler may have kicked the player it was asked about.
        human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (human == nullptr || human->GetNetworkID() != request.networkId) {
            Framework::Logging::GetLogger(kLogger)->debug("Body {} was gone by the time its spawn was decided", request.networkId);
            return;
        }

        KCDC::Shared::RPC::SpawnPlayer spawn;
        if (_choice.chosen) {
            spawn.x         = _choice.position.x;
            spawn.y         = _choice.position.y;
            spawn.z         = _choice.position.z;
            spawn.rotationX = _choice.rotation.x;
            spawn.rotationY = _choice.rotation.y;
            spawn.rotationZ = _choice.rotation.z;
            spawn.rotationW = _choice.rotation.w;
            spawn.placed    = true;
        }

        if (!Send(human, spawn)) {
            Framework::Logging::GetLogger(kLogger)->warn("Could not answer the spawn request from body {}", request.networkId);
            return;
        }

        if (spawn.placed) {
            Framework::Logging::GetLogger(kLogger)->info("Body {} spawns at ({:.1f}, {:.1f}, {:.1f})", request.networkId, spawn.x, spawn.y, spawn.z);
        }
        else {
            Framework::Logging::GetLogger(kLogger)->debug("Body {} has no spawn point; it keeps the level's own start point", request.networkId);
        }
    }

    void PlayerService::ReportCarry(const KCDC::Shared::RPC::PlayerCarry &report, MafiaNet::PeerGuid owner) {
        auto *carrier = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (carrier == nullptr) {
            return;
        }

        if (!report.carrying) {
            // Only the body on record: a put-down for a carry the server
            // already ended, or never accepted, is the tail of that one.
            if (carrier->carriedNetworkId == report.carriedNetworkId) {
                EndCarry(carrier, false);
            }
            return;
        }

        // A grab while the record still holds another body: that put-down was
        // lost or is still in flight, and a shoulder holds one body.
        if (carrier->carriedNetworkId != 0 && carrier->carriedNetworkId != report.carriedNetworkId) {
            EndCarry(carrier, false);
        }
        if (carrier->carriedNetworkId == report.carriedNetworkId) {
            return;
        }

        // The carrier's game has already picked the body up, so every refusal
        // is the same answer: tell that client to put it down again. The grab
        // prompt is the game's own and gated there on the body being down;
        // what is checked here is what one client cannot know about the rest.
        auto *carried       = Players::Find(report.carriedNetworkId);
        auto *otherCarrier  = carried != nullptr ? Players::CarrierOf(carried->GetNetworkID()) : nullptr;
        const char *refusal = nullptr;
        if (carried == nullptr || carried == carrier) {
            refusal = "there is no such player";
        }
        else if (otherCarrier != nullptr) {
            refusal = "somebody else is carrying them";
        }
        else if (carried->carriedNetworkId != 0) {
            refusal = "they are carrying somebody";
        }
        else if (!MafiaNet::VirtualWorldsCanSee(carried->GetVirtualWorld(), carrier->GetVirtualWorld()) || glm::distance(carried->position, carrier->position) > kMaxCarryDistance) {
            refusal = "they are not within reach";
        }
        else if (!KCDC::Scripting::Events::PlayerCarrying(carrier->GetNetworkID(), carried->GetNetworkID())) {
            refusal = "a playerCarrying handler refused it";
        }

        if (refusal != nullptr) {
            Framework::Logging::GetLogger(kLogger)->info("Player '{}' may not carry {}: {}", carrier->nickname, report.carriedNetworkId, refusal);
            Players::PutDown(carrier, true);
            return;
        }

        carrier->carriedNetworkId = carried->GetNetworkID();
        Framework::Logging::GetLogger(kLogger)->info("Player '{}' picked up '{}'", carrier->nickname, carried->nickname);
        KCDC::Scripting::Events::PlayerCarried(carrier->GetNetworkID(), carried->GetNetworkID());
    }

    void PlayerService::EndCarry(KCDC::Shared::Entities::HumanEntity *carrier, bool orderClient) {
        if (carrier == nullptr || carrier->carriedNetworkId == 0) {
            return;
        }
        if (orderClient) {
            Players::PutDown(carrier, true);
        }
        const std::uint64_t carriedNetworkId = std::exchange(carrier->carriedNetworkId, 0);
        Framework::Logging::GetLogger(kLogger)->info("Player '{}' put down {}", carrier->nickname, carriedNetworkId);
        KCDC::Scripting::Events::PlayerPutDown(carrier->GetNetworkID(), carriedNetworkId);
    }

    KCDC::Shared::Game::ItemCarryState PlayerService::CarryStateOf(KCDC::Shared::Entities::HumanEntity &player) {
        KCDC::Shared::Game::ItemCarryState state;
        state.canAct       = player.soul.CanAct();
        state.mounted      = KCDC::Features::Horse::Horses::RiddenBy(player.GetNetworkID()) != nullptr;
        state.carryingBody = player.carriedNetworkId != 0;
        state.carryingItem = player.carriedItemClass.IsValid();
        return state;
    }

    bool PlayerService::OrderItemCarry(KCDC::Shared::Entities::HumanEntity *player, const KCDC::Shared::RPC::ItemClassId &itemClass, KCDC::Shared::Entities::GroundItemEntity *groundItem) {
        if (player == nullptr || !itemClass.IsValid() || KCDC::Shared::Game::ItemCarryRefusal(CarryStateOf(*player)) != nullptr) {
            return false;
        }

        auto &groundItems                 = KCDC::Server::Get()->GetGroundItems();
        const std::uint64_t groundItemNetworkId = groundItem != nullptr ? groundItem->GetNetworkID() : 0;
        if (groundItem != nullptr && (groundItems.CarryRefusal(*groundItem, *player) != nullptr || !groundItems.ClaimForCarry(groundItemNetworkId, player->GetNetworkID()))) {
            return false;
        }

        if (!Players::CarryItem(player, itemClass, groundItemNetworkId)) {
            if (groundItemNetworkId != 0) {
                groundItems.ReleaseCarryClaim(groundItemNetworkId);
            }
            return false;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        std::erase_if(_itemCarryOrders, [now](const auto &order) { return order.second.expiresAt <= now; });
        _itemCarryOrders[player->GetNetworkID()] = ItemCarryOrder {itemClass, groundItemNetworkId, static_cast<std::uint64_t>(now + kItemCarryOrderTimeoutMs)};
        return true;
    }

    void PlayerService::RequestGroundItemCarry(const KCDC::Shared::RPC::RequestCarryGroundItem &request, MafiaNet::PeerGuid owner) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player      = replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        auto *groundItem  = replication->GetEntity<KCDC::Shared::Entities::GroundItemEntity>(request.groundItemNetworkId);
        if (player == nullptr || groundItem == nullptr || !groundItem->carryable) {
            return;
        }

        // What the server would refuse anyway is refused before the gamemode
        // is asked, so a handler only ever weighs a carry that can happen.
        const char *refusal = KCDC::Shared::Game::ItemCarryRefusal(CarryStateOf(*player));
        if (refusal == nullptr) {
            refusal = KCDC::Server::Get()->GetGroundItems().CarryRefusal(*groundItem, *player);
        }
        if (refusal == nullptr && !KCDC::Scripting::Events::PlayerCarryingItem(player->GetNetworkID(), groundItem->item.itemClass, groundItem->GetNetworkID())) {
            refusal = "a playerCarryingItem handler refused it";
        }
        if (refusal != nullptr) {
            Framework::Logging::GetLogger(kLogger)->info("Player '{}' may not carry ground item {}: {}", player->nickname, groundItem->GetNetworkID(), refusal);
            return;
        }
        OrderItemCarry(player, groundItem->item.itemClass, groundItem);
    }

    void PlayerService::RefuseItemCarry(KCDC::Shared::Entities::HumanEntity *carrier, std::uint64_t groundItemNetworkId) {
        if (groundItemNetworkId != 0) {
            KCDC::Server::Get()->GetGroundItems().ReleaseCarryClaim(groundItemNetworkId);
        }
        Players::PutDownItem(carrier, true, true);
    }

    void PlayerService::ReportItemCarry(const KCDC::Shared::RPC::PlayerCarryItem &report, MafiaNet::PeerGuid owner) {
        auto *carrier = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner);
        if (carrier == nullptr) {
            return;
        }

        if (!report.carrying) {
            // Only the item on record: a put-down for a carry the server
            // already ended, or never accepted, is the tail of that one.
            if (carrier->carriedItemClass == report.itemClass) {
                EndItemCarry(carrier, report.dropped, &report);
            }
            return;
        }

        // The put-down clip started; the item leaves the arms when it ends.
        if (report.puttingDown) {
            if (carrier->carriedItemClass == report.itemClass) {
                carrier->carriedItemPuttingDown = true;
            }
            return;
        }

        // A pick-up while the record still holds another item: that put-down
        // was lost or is still in flight, and a pair of arms holds one item.
        if (carrier->carriedItemClass.IsValid() && carrier->carriedItemClass != report.itemClass) {
            EndItemCarry(carrier, true, nullptr);
        }
        if (carrier->carriedItemClass == report.itemClass) {
            return;
        }

        // The order this carry answers, if the server gave one.
        const auto order   = _itemCarryOrders.find(carrier->GetNetworkID());
        const bool ordered = order != _itemCarryOrders.end() && order->second.expiresAt > MafiaNet::GetTime() && order->second.itemClass == report.itemClass && order->second.groundItemNetworkId == report.groundItemNetworkId;
        if (order != _itemCarryOrders.end()) {
            _itemCarryOrders.erase(order);
        }

        auto &groundItems = KCDC::Server::Get()->GetGroundItems();
        auto *groundItem  = report.groundItemNetworkId != 0 ? Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::GroundItemEntity>(report.groundItemNetworkId) : nullptr;

        KCDC::Shared::Game::ItemCarryReport judged;
        judged.classKnown          = KCDC::Shared::Game::FindItemClass(report.itemClass) != nullptr;
        judged.walkBodies          = KCDC::Shared::Game::CarryWalkBodies(report.itemClass);
        judged.ordered             = ordered;
        judged.groundItemNetworkId = report.groundItemNetworkId;
        judged.groundClaimed       = groundItem != nullptr && groundItems.HoldsCarryClaim(report.groundItemNetworkId, carrier->GetNetworkID());
        judged.groundClaimable     = groundItem != nullptr && groundItem->item.itemClass == report.itemClass && groundItems.CarryRefusal(*groundItem, *carrier) == nullptr;
        const char *refusal        = nullptr;
        KCDC::Shared::Game::ItemCarryVerdict verdict = KCDC::Shared::Game::JudgeItemCarryReport(judged, refusal);
        if (verdict == KCDC::Shared::Game::ItemCarryVerdict::AskGamemode && !KCDC::Scripting::Events::PlayerCarryingItem(carrier->GetNetworkID(), report.itemClass, groundItem != nullptr ? groundItem->GetNetworkID() : 0)) {
            verdict = KCDC::Shared::Game::ItemCarryVerdict::Refuse;
            refusal = "a playerCarryingItem handler refused it";
        }
        if (verdict == KCDC::Shared::Game::ItemCarryVerdict::Refuse) {
            Framework::Logging::GetLogger(kLogger)->info("Player '{}' may not carry item {:016x}{:016x}: {}", carrier->nickname, report.itemClass.low, report.itemClass.high, refusal);
            RefuseItemCarry(carrier, judged.groundClaimed ? report.groundItemNetworkId : 0);
            return;
        }

        carrier->carriedItemClass           = report.itemClass;
        carrier->carriedGroundItemNetworkId = report.groundItemNetworkId;
        carrier->carriedItemPuttingDown     = false;
        Framework::Logging::GetLogger(kLogger)->info("Player '{}' carries item {:016x}{:016x}", carrier->nickname, report.itemClass.low, report.itemClass.high);
        if (groundItem != nullptr) {
            // Raised while the stack can still be read, as an inventory pickup
            // is; it leaves the ground once every client has taken it up.
            KCDC::Scripting::Events::GroundItemPickedUp(groundItem->GetNetworkID(), carrier->GetNetworkID());
            groundItems.RetireCarried(groundItem->GetNetworkID());
        }
        KCDC::Scripting::Events::PlayerCarriedItem(carrier->GetNetworkID(), report.itemClass, report.groundItemNetworkId);
    }

    void PlayerService::EndItemCarry(KCDC::Shared::Entities::HumanEntity *carrier, bool dropped, const KCDC::Shared::RPC::PlayerCarryItem *report) {
        if (carrier == nullptr || !carrier->carriedItemClass.IsValid()) {
            return;
        }
        const KCDC::Shared::RPC::ItemClassId itemClass = std::exchange(carrier->carriedItemClass, KCDC::Shared::RPC::ItemClassId {});
        carrier->carriedGroundItemNetworkId            = 0;
        carrier->carriedItemPuttingDown                = false;

        // What went to the ground stays there, as a prop anybody can take up
        // again; one a pile took in is the pile's. It starts where the
        // carrier's game let it go, when that was reported.
        const bool poseKnown                           = report != nullptr && report->dropPoseKnown;
        KCDC::Shared::Entities::GroundItemEntity *prop = dropped ? KCDC::Server::Get()->GetGroundItems().PutDownCarried(*carrier, itemClass, poseKnown ? &report->dropPosition : nullptr, poseKnown ? &report->dropRotation : nullptr) : nullptr;
        Framework::Logging::GetLogger(kLogger)->info("Player '{}' put down item {:016x}{:016x}{}", carrier->nickname, itemClass.low, itemClass.high, prop != nullptr ? fmt::format(" as ground item {}", prop->GetNetworkID()) : std::string());
        KCDC::Scripting::Events::PlayerPutDownItem(carrier->GetNetworkID(), itemClass, prop != nullptr ? prop->GetNetworkID() : 0);
    }

    void PlayerService::OnPlayerDisconnect(MafiaNet::PeerGuid owner) {
        // A carry ends with either of its bodies. Whoever stays is told
        // nothing: their game lets go of the body as its puppet is destroyed.
        if (auto *leaving = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(owner)) {
            _statSnapshots.erase(leaving->GetNetworkID());
            EndCarry(Players::CarrierOf(leaving->GetNetworkID()), false);
            EndCarry(leaving, false);
            // Their arms go with them; what they carried is left where they
            // stood, for somebody else to take up.
            EndItemCarry(leaving, true, nullptr);
            _itemCarryOrders.erase(leaving->GetNetworkID());
        }

        _deaths.erase(owner);
        _awaitingReady.erase(owner);
        // Do not let the choice reach whoever takes this slot next.
        if (_resolvingActive && _resolving == owner) {
            _choice = {};
        }
    }
} // namespace KCDC::Features::Player
