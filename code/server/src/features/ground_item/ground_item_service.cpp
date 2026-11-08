#include "ground_item_service.h"

#include "core/server.h"
#include "features/ground_item/ground_item_scripting.h"
#include "features/ground_item/ground_item_verbs.h"
#include "features/ground_item/ground_stock.h"
#include "features/player/player_roster.h"

#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/ground_item/ground_item_inventory_message.h"
#include "shared/features/ground_item/ground_item_rpc.h"
#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace KCDC::Features::GroundItem {
    namespace {
        constexpr const char *kLogger                  = "GroundItem";
        constexpr MafiaNet::Time kReservationTimeoutMs = 15000;
        // A stack whose simulator never reports it resting is settled where it is after this.
        constexpr MafiaNet::Time kFallTimeoutMs = 10000;

        bool IsWorldPose(const glm::vec3 &position, const glm::quat &rotation) {
            if (!Shared::RPC::Detail::IsFinitePose(position, rotation)) {
                return false;
            }
            const auto inside = [](float value) {
                return value >= Shared::Entities::HumanEntity::kWorldMin && value <= Shared::Entities::HumanEntity::kWorldMax;
            };
            const float lengthSquared = glm::dot(rotation, rotation);
            return inside(position.x) && inside(position.y) && inside(position.z) && lengthSquared > 0.25F && lengthSquared < 4.0F;
        }
    } // namespace

    void GroundItemService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        net->RegisterRPC<Shared::RPC::GroundItemDropRequest>([this](const Shared::RPC::GroundItemDropRequest &request, MafiaNet::Packet *packet) {
            OnDropRequest(request, packet);
        });
        net->RegisterRPC<Shared::RPC::GroundItemPickupStarted>([this](const Shared::RPC::GroundItemPickupStarted &pickup, MafiaNet::Packet *packet) {
            OnPickupStarted(pickup, packet);
        });
        net->RegisterRPC<Shared::RPC::GroundItemPickupCompleted>([this](const Shared::RPC::GroundItemPickupCompleted &pickup, MafiaNet::Packet *packet) {
            OnPickupCompleted(pickup, packet);
        });
    }

    void GroundItemService::OnDropRequest(const Shared::RPC::GroundItemDropRequest &request, MafiaNet::Packet *packet) {
        auto *source = packet != nullptr && request.valid ? Features::Player::Players::FromPacket(packet) : nullptr;
        if (source == nullptr || !IsWorldPose(source->position, source->rotation)) {
            return;
        }
        auto &inventories = Server::Get()->GetInventory();
        auto *inventory   = inventories.Find(source->GetNetworkID());
        std::vector<Shared::Inventory::DropUnit> units;
        // A prior action may still be awaiting acknowledgement. Exact rows
        // and amounts validate this entire selection against current stock.
        if (inventory == nullptr || !inventories.IsInitialized(source->GetNetworkID()) || !inventories.Admit(source->GetNetworkID()) || !GroundStock::ReadDropRequest(request.body, units)) {
            return;
        }
        // Count the physical stacks after splitting non-stackable rows, before
        // any inventory mutation. One row of fourteen torches needs fourteen.
        const auto occupied = _counts.Count(source->GetNetworkID());
        const auto available = GroundStockCounts::kMaximumPerPlayer - std::min(occupied, GroundStockCounts::kMaximumPerPlayer);
        const auto drop = GroundStock::Drop(*inventory, units, available);
        if (!drop.result.ok) {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} drop refused: {}", source->GetNetworkID(), drop.result.code);
            return;
        }
        // Scripts hear of the removal before the stacks it put down.
        inventories.DispatchChanges();
        for (const auto &stack : drop.stacks) {
            auto *groundItem = GroundItems::Drop(stack.item, *source, stack.retiresHeldItem, false);
            if (groundItem == nullptr) {
                // The units already left the inventory; what could not be put
                // down goes straight back rather than vanishing.
                GroundStock::Pickup(*inventory, stack.item, "dropRefund", false);
                continue;
            }
            _counts.Add(groundItem->GetNetworkID(), source->GetNetworkID());
            _falls[groundItem->GetNetworkID()] = Fall {groundItem->position, static_cast<std::uint64_t>(MafiaNet::GetTime() + kFallTimeoutMs)};
        }
        inventories.DispatchChanges();
    }

    void GroundItemService::OnPickupStarted(const Shared::RPC::GroundItemPickupStarted &pickup, MafiaNet::Packet *packet) {
        if (packet == nullptr || !pickup.IsSane()) {
            return;
        }

        auto *replication             = Framework::CoreModules::GetReplication();
        auto *entity                  = replication != nullptr ? replication->GetEntity<Shared::Entities::GroundItemEntity>(pickup.groundItemNetworkId) : nullptr;
        const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
        auto *claimant                = replication != nullptr ? replication->GetViewerAs<Shared::Entities::HumanEntity>(guid) : nullptr;
        // A carryable prop is taken up in the arms, never into an inventory,
        // and one a carry already holds is nobody else's.
        if (entity == nullptr || claimant == nullptr || claimant->GetVirtualWorld() != entity->GetVirtualWorld() || entity->carryable || _carryClaims.IsHeld(entity->GetNetworkID(), MafiaNet::GetTime())) {
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Lost);
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        auto [it, inserted]      = _reservations.try_emplace(pickup.groundItemNetworkId, Reservation {guid, pickup.pickupToken, static_cast<std::uint64_t>(now + kReservationTimeoutMs)});
        if (!inserted && it->second.expiresAt <= now) {
            it->second = Reservation {guid, pickup.pickupToken, static_cast<std::uint64_t>(now + kReservationTimeoutMs)};
        }

        const bool ownsReservation = it->second.claimant == guid && it->second.pickupToken == pickup.pickupToken;
        SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, ownsReservation ? Shared::RPC::GroundItemPickupDisposition::Reserved : Shared::RPC::GroundItemPickupDisposition::Lost);
    }

    void GroundItemService::OnPickupCompleted(const Shared::RPC::GroundItemPickupCompleted &pickup, MafiaNet::Packet *packet) {
        if (packet == nullptr || !pickup.IsSane()) {
            return;
        }

        const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
        auto reservation              = _reservations.find(pickup.groundItemNetworkId);
        if (reservation == _reservations.end() || reservation->second.claimant != guid || reservation->second.pickupToken != pickup.pickupToken) {
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Lost);
            return;
        }

        if (reservation->second.expiresAt <= MafiaNet::GetTime()) {
            _reservations.erase(reservation);
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Released);
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->GetEntity<Shared::Entities::GroundItemEntity>(pickup.groundItemNetworkId) : nullptr;
        if (entity == nullptr) {
            _reservations.erase(reservation);
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Lost);
            return;
        }

        // The server gives the stack itself; what the claimant's game did
        // with its native copy is reconciled by the inventory sync.
        auto *claimant    = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
        auto &inventories = Server::Get()->GetInventory();
        auto *inventory   = claimant != nullptr && inventories.IsReady(claimant->GetNetworkID()) ? inventories.Find(claimant->GetNetworkID()) : nullptr;
        _reservations.erase(reservation);
        if (!pickup.succeeded || inventory == nullptr || !GroundStock::PickupRefusal(claimant->position, claimant->GetVirtualWorld(), claimant->soul.CanAct(), entity->restPosition, entity->GetVirtualWorld(), entity->resting && !_falls.contains(entity->GetNetworkID())).empty()) {
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Released);
            return;
        }
        const auto granted = GroundStock::Pickup(*inventory, entity->item);
        if (!granted.ok) {
            SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Released);
            return;
        }
        SendPickupResult(guid, pickup.groundItemNetworkId, pickup.pickupToken, Shared::RPC::GroundItemPickupDisposition::Won);
        inventories.DispatchChanges();

        // Raised while the stack is still here, so a handler can read what was
        // taken and by whom. The despawn that follows raises groundItemDestroy
        // of its own, as every other way of removing one does.
        Scripting::Events::GroundItemPickedUp(entity->GetNetworkID(), claimant->GetNetworkID());
        GroundItems::Destroy(entity);
    }

    void GroundItemService::SendPickupResult(MafiaNet::PeerGuid guid, std::uint64_t groundItemNetworkId, std::uint64_t pickupToken, Shared::RPC::GroundItemPickupDisposition disposition) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::GroundItemPickupResult result;
        result.groundItemNetworkId = groundItemNetworkId;
        result.pickupToken         = pickupToken;
        result.disposition         = disposition;
        net->SendRPC(result, MafiaNet::ToGuid(guid));
    }

    void GroundItemService::Update() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        std::vector<std::uint64_t> expired;
        for (const auto &[networkId, reservation] : _reservations) {
            if (reservation.expiresAt <= now) {
                SendPickupResult(reservation.claimant, networkId, reservation.pickupToken, Shared::RPC::GroundItemPickupDisposition::Released);
                expired.push_back(networkId);
            }
        }
        for (const std::uint64_t networkId : expired) {
            _reservations.erase(networkId);
        }

        // Falls end when the simulator reports the stack resting, or when it
        // takes too long; either way the server keeps the pose from then on.
        auto *replication = Framework::CoreModules::GetReplication();
        for (auto fall = _falls.begin(); fall != _falls.end();) {
            auto *groundItem = replication != nullptr ? replication->GetEntity<Shared::Entities::GroundItemEntity>(fall->first) : nullptr;
            if (groundItem == nullptr) {
                fall = _falls.erase(fall);
                continue;
            }
            if (!groundItem->resting && fall->second.deadline > now) {
                ++fall;
                continue;
            }
            Settle(*groundItem, fall->second);
            fall = _falls.erase(fall);
        }

        // Taken up by a carry and seen into everybody's arms by now.
        for (const std::uint64_t networkId : _carryClaims.TakeDue(now)) {
            GroundItems::Destroy(replication != nullptr ? replication->GetEntity<Shared::Entities::GroundItemEntity>(networkId) : nullptr);
        }
    }

    const char *GroundItemService::CarryRefusal(Shared::Entities::GroundItemEntity &groundItem, Shared::Entities::HumanEntity &carrier) const {
        if (groundItem.item.amount != 1) {
            return "a stack is not carried in the arms";
        }
        // The game's pick-up reaches for the item, so it has to be within an
        // arm's length of where the server keeps the stack.
        if (!GroundStock::PickupRefusal(carrier.position, carrier.GetVirtualWorld(), true, groundItem.restPosition, groundItem.GetVirtualWorld(), groundItem.resting).empty()) {
            return "it is not resting within their reach";
        }
        if (_reservations.contains(groundItem.GetNetworkID()) || !_carryClaims.IsClaimable(groundItem.GetNetworkID(), carrier.GetNetworkID(), MafiaNet::GetTime())) {
            return "somebody else is taking it";
        }
        return nullptr;
    }

    bool GroundItemService::ClaimForCarry(std::uint64_t groundItemNetworkId, std::uint64_t carrierNetworkId) {
        return !_reservations.contains(groundItemNetworkId) && _carryClaims.Claim(groundItemNetworkId, carrierNetworkId, MafiaNet::GetTime());
    }

    bool GroundItemService::HoldsCarryClaim(std::uint64_t groundItemNetworkId, std::uint64_t carrierNetworkId) const {
        return _carryClaims.Holds(groundItemNetworkId, carrierNetworkId, MafiaNet::GetTime());
    }

    void GroundItemService::ReleaseCarryClaim(std::uint64_t groundItemNetworkId) {
        _carryClaims.Release(groundItemNetworkId);
    }

    void GroundItemService::RetireCarried(std::uint64_t groundItemNetworkId) {
        _carryClaims.Retire(groundItemNetworkId, MafiaNet::GetTime());
    }

    Shared::Entities::GroundItemEntity *GroundItemService::PutDownCarried(Shared::Entities::HumanEntity &source, const Shared::RPC::ItemClassId &itemClass, const glm::vec3 *dropPosition, const glm::quat *dropRotation) {
        // A server-authored stack: its identity is ours to mint, and a stack
        // without one is no stack `CanPlace` lets down.
        Shared::RPC::InventoryItem item;
        item.inventoryItemId = NextServerItemId();
        item.itemClass       = itemClass;
        item.amount          = 1;
        if (!_counts.CanAdd(source.GetNetworkID(), 1)) {
            return nullptr;
        }
        auto *groundItem = GroundItems::Drop(item, source, false, true);
        if (groundItem == nullptr) {
            return nullptr;
        }
        // The item already lies where the carrier's hands let it go; the prop
        // starts there, within a fall of the carrier, so nobody's copy of it
        // is moved to the server's guess first.
        if (dropPosition != nullptr && dropRotation != nullptr && Shared::Game::IsWorldPosition(*dropPosition) && Shared::Game::IsUsableRotation(*dropRotation) && glm::distance(*dropPosition, source.position) <= GroundStock::kMaximumFallDistance) {
            groundItem->position = *dropPosition;
            groundItem->rotation = glm::normalize(*dropRotation);
        }
        _counts.Add(groundItem->GetNetworkID(), source.GetNetworkID());
        _falls[groundItem->GetNetworkID()] = Fall {groundItem->position, static_cast<std::uint64_t>(MafiaNet::GetTime() + kFallTimeoutMs)};
        return groundItem;
    }

    void GroundItemService::Settle(Shared::Entities::GroundItemEntity &groundItem, const Fall &fall) const {
        const glm::vec3 reported = groundItem.resting ? groundItem.restPosition : groundItem.position;
        const glm::quat rotation = groundItem.resting ? groundItem.restRotation : groundItem.rotation;
        groundItem.restPosition  = GroundStock::SettledPosition(fall.origin, reported);
        groundItem.restRotation  = Shared::Game::IsUsableRotation(rotation) ? glm::normalize(rotation) : glm::identity<glm::quat>();
        groundItem.position      = groundItem.restPosition;
        groundItem.rotation      = groundItem.restRotation;
        groundItem.velocity      = glm::vec3(0.0F);
        groundItem.resting       = true;
        // The simulator is told the pose the server settled on before it
        // loses the stack; unowned, it replicates to every client and no
        // delegation pass hands it out again, as a resting stack has no
        // delegation policy.
        groundItem.ForceState();
        groundItem.SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
    }

    bool GroundItemService::CanPlaceFor(std::uint64_t source, Shared::RPC::InventoryItem item) const {
        GroundStock::ApplyClassDefaults(item);
        return _counts.CanAdd(source, 1) && GroundStock::CanPlace(item);
    }

    Shared::Entities::GroundItemEntity *GroundItemService::PlaceFor(std::uint64_t source, Shared::RPC::InventoryItem item, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, std::uint64_t recoveredItemWuid) {
        if (!CanPlaceFor(source, item)) {
            return nullptr;
        }
        auto *groundItem = GroundItems::Spawn(std::move(item), position, rotation, virtualWorld, false);
        if (groundItem != nullptr) {
            groundItem->sourcePlayerNetworkId = source;
            groundItem->recoveredItemWuid     = recoveredItemWuid;
            _counts.Add(groundItem->GetNetworkID(), source);
        }
        return groundItem;
    }

    void GroundItemService::Forget(std::uint64_t groundItemNetworkId) {
        _counts.Remove(groundItemNetworkId);
        _falls.erase(groundItemNetworkId);
        _reservations.erase(groundItemNetworkId);
        _carryClaims.Forget(groundItemNetworkId);
    }

    void GroundItemService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // A stack its simulator was still dropping is settled where it was
        // last reported, rather than waiting out its timeout.
        for (auto fall = _falls.begin(); fall != _falls.end();) {
            auto *groundItem = replication->GetEntity<Shared::Entities::GroundItemEntity>(fall->first);
            if (groundItem == nullptr || groundItem->ownerGUID != guid) {
                ++fall;
                continue;
            }
            Settle(*groundItem, fall->second);
            fall = _falls.erase(fall);
        }

        std::vector<std::uint64_t> claimed;
        for (const auto &[networkId, reservation] : _reservations) {
            if (reservation.claimant == guid) {
                claimed.push_back(networkId);
            }
        }
        for (const std::uint64_t networkId : claimed) {
            _reservations.erase(networkId);
        }
    }
} // namespace KCDC::Features::GroundItem
