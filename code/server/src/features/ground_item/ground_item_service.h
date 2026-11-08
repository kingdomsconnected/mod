#pragma once

#include "features/ground_item/ground_stock.h"

#include "shared/features/ground_item/ground_item_entity.h"

#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>

namespace KCDC::Shared::RPC {
    enum class GroundItemPickupDisposition : std::uint8_t;
    struct GroundItemDropRequest;
    struct GroundItemPickupCompleted;
    struct GroundItemPickupStarted;
} // namespace KCDC::Shared::RPC

namespace KCDC::Features::GroundItem {
    /**
     * Stock on the ground: a player's drop takes the units out of their
     * inventory and puts them down, a pickup gives one whole stack back, and
     * the first claim wins. The nearest client simulates a dropped stack's
     * fall; once it rests the server takes it back and keeps its pose.
     */
    class GroundItemService final {
      public:
        void Install();
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * Mints the identity for a stack the server authors itself.
         *
         * From a range no client mints into, and never reused within a
         * session, which is all the uniqueness anything asks of it.
         */
        std::uint64_t NextServerItemId() {
            return _nextServerItemId++;
        }

        /** A stack left the ground; called by `GroundItems::Destroy`. */
        void Forget(std::uint64_t groundItemNetworkId);

        /** Whether `source` may put one more stack down, and every receiver could build it. */
        bool CanPlaceFor(std::uint64_t source, Shared::RPC::InventoryItem item) const;
        /** Lays down a resting stack a player's own action left, counted against their stacks on the ground. */
        Shared::Entities::GroundItemEntity *PlaceFor(std::uint64_t source, Shared::RPC::InventoryItem item, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, std::uint64_t recoveredItemWuid);

        // --- Carrying ------------------------------------------------------

        /**
         * Why `carrier` may not take `groundItem` up in their arms now, or
         * null when they may: it is resting within their reach and in their
         * world, and neither an inventory pickup nor another carry holds it.
         */
        const char *CarryRefusal(Shared::Entities::GroundItemEntity &groundItem, Shared::Entities::HumanEntity &carrier) const;

        /** Reserves a stack for `carrierNetworkId` to take up, until their carry reports in or the claim lapses. */
        bool ClaimForCarry(std::uint64_t groundItemNetworkId, std::uint64_t carrierNetworkId);

        /** Whether `carrierNetworkId` holds the carry claim on this stack. */
        bool HoldsCarryClaim(std::uint64_t groundItemNetworkId, std::uint64_t carrierNetworkId) const;

        void ReleaseCarryClaim(std::uint64_t groundItemNetworkId);

        /**
         * A carry took the stack up. It is destroyed a moment later rather
         * than now: every other client takes its own copy into the carrier's
         * arms with the game's pick-up, and needs the stack still there to.
         */
        void RetireCarried(std::uint64_t groundItemNetworkId);

        /**
         * Puts down the carryable prop a carry dropped, falling from the
         * carrier's hands, counted against their stacks on the ground. Every
         * client builds it from the item that carry dropped. Null when the
         * carrier has no room left or the class cannot be placed.
         */
        Shared::Entities::GroundItemEntity *PutDownCarried(Shared::Entities::HumanEntity &source, const Shared::RPC::ItemClassId &itemClass, const glm::vec3 *dropPosition, const glm::quat *dropRotation);

      private:
        struct Reservation {
            MafiaNet::PeerGuid claimant = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t pickupToken   = 0;
            std::uint64_t expiresAt     = 0;
        };

        /** A dropped stack still falling under its simulator. */
        struct Fall {
            glm::vec3 origin;
            std::uint64_t deadline = 0;
        };

        /** Ends a fall: the server keeps the settled pose and no client simulates the stack any more. */
        void Settle(Shared::Entities::GroundItemEntity &groundItem, const Fall &fall) const;

        void OnDropRequest(const Shared::RPC::GroundItemDropRequest &request, MafiaNet::Packet *packet);
        void OnPickupStarted(const Shared::RPC::GroundItemPickupStarted &pickup, MafiaNet::Packet *packet);
        void OnPickupCompleted(const Shared::RPC::GroundItemPickupCompleted &pickup, MafiaNet::Packet *packet);
        void SendPickupResult(MafiaNet::PeerGuid guid, std::uint64_t groundItemNetworkId, std::uint64_t pickupToken, Shared::RPC::GroundItemPickupDisposition disposition) const;

        std::unordered_map<std::uint64_t, Reservation> _reservations;
        GroundCarryClaims _carryClaims;
        std::unordered_map<std::uint64_t, Fall> _falls;
        GroundStockCounts _counts;
        std::uint64_t _nextServerItemId = Shared::Entities::GroundItemEntity::kServerAuthoredItemIdBase;
    };
} // namespace KCDC::Features::GroundItem
