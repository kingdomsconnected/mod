#pragma once

#include "core/feature_registry.h"

#include "cart.h"

#include "shared/features/cart/cart_motion.h"
#include "shared/features/cart/cart_seat.h"

#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor_action_cart.h"
#include "sdk/wh/entitymodule/c_cart.h"
#include "sdk/wh/entitymodule/e_cart_actor_state.h"
#include "sdk/wh/entitymodule/e_cart_position.h"
#include "sdk/wh/entitymodule/s_actor_action_cart_params.h"

#include <mafianet/types.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Cart {
    /**
     * Everything about carts that is not about one cart.
     *
     * Who sits in which cart as the engine last said, the local player's own
     * seat and the requests it has in flight, the drivers' hands on the
     * reins, and the few questions the engine's hooks ask -- is a cart one of
     * ours, is a body another player's copy in one -- all live here, on one
     * object the hooks and the tick share.
     */
    class CartService final: public KCDC::Core::IFeatureService {
      public:
        static CartService &Get();

        /** Game thread: builds, drives and follows every cart, then seats every body in them. */
        void Update() override;

        /**
         * Session ended: forgets the seat register and the local ride.
         */
        void Reset() override;

        // --- Questions the hooks ask -------------------------------------------

        /** The replicated cart one of whose `WHCart`s is `cartEntityId`, or nullptr for a cart the level placed. Game thread. */
        Cart *FindByCartEntity(SDK::EntityId cartEntityId) const;

        /** Whether the `C_Cart` is one this mod built. Any thread: read against a table the tick publishes. */
        bool IsModCart(const SDK::wh::entitymodule::C_Cart *cart) const;

        /** Whether a path cursor belongs to one of this mod's carts. Any thread: read against a table the tick publishes. */
        bool IsModCursor(const SDK::wh::entitymodule::S_CartPathCursor *cursor) const;

        // --- The engine's seat edges, raised by the SetCartActorState hook ------

        /**
         * A body's stay in a cart moved to `state`. Recorded under a lock --
         * the action can be updated off the game thread -- and read back by
         * the tick.
         */
        void NotifyCartState(SDK::wh::entitymodule::C_ActorActionCart *action, SDK::wh::entitymodule::E_CartActorState::Type state);

        /** Whether a body is in a cart as far as the engine is concerned, from the walk to the seat to the climb down. Any thread. */
        bool IsInCart(SDK::EntityId actorEntityId) const;

        // --- The local player's mount ------------------------------------------

        /**
         * The game's own mount prompt was used on `cartEntityId`. For a cart of
         * this mod's, the player is not seated -- the server is asked -- and
         * true is returned to keep the native mount from running. Game thread.
         */
        bool InterceptPromptMount(SDK::EntityId cartEntityId, bool frontSeat);

        /**
         * The position a cart action is asked for with: the seat's own while
         * this module runs MountPlayer, which only knows the bench and the
         * right rail; `requested` otherwise. Game thread.
         */
        SDK::wh::entitymodule::E_CartPosition::Type MountPosition(SDK::wh::entitymodule::E_CartPosition::Type requested) const {
            return _mountPosition.value_or(requested);
        }

        /**
         * Takes a cart's entities out of the world -- once nobody sits in it.
         *
         * A cart removed from under a seated body leaves that body in a seat
         * that no longer exists: the local player sits on nothing with the
         * dismount key dead, and every later mount fails. So a cart somebody
         * is in is hidden, everyone in it is got down the quick way, and its
         * entities go when the last one is out, or after ten seconds whatever
         * happens. `entities` are removed in the order given.
         */
        void Retire(std::vector<SDK::EntityId> cartEntityIds, std::vector<SDK::EntityId> entities);

        /** The reins, read off the mod's keys on the game's `cart` action map. */
        static Shared::Game::CartControls ReadControls();

      private:
        /** Removes what `Retire` was holding once its cart is empty. */
        void UpdateRetired();

        /** Removes the cart's entities, now. */
        static void RemoveCartEntities(const std::vector<SDK::EntityId> &entities);

        /** Drops the stays of bodies no longer in the world; see the definition. */
        void PruneStays();

        /** Whether any body's stay is in one of `cartEntityIds`. */
        bool HasOccupants(const std::vector<SDK::EntityId> &cartEntityIds) const;

        /** A cart waiting for its occupants to be off before it is removed. */
        struct RetiredCart {
            std::vector<SDK::EntityId> cartEntityIds;
            std::vector<SDK::EntityId> entities;
            MafiaNet::Time deadline = 0;
        };
        std::vector<RetiredCart> _retired;

        /** Publishes the table the any-thread questions read. */
        void PublishCarts();

        /** Seats every body in a cart where the wire says, and lets out any the wire no longer has. */
        void UpdateSeats();

        /** The reins in the driver's hands, the local player's or a copy's; see the definition. */
        void UpdateDriverPoses();

        /** The local player's own seat: climb into it, or out of one the server took away. */
        void UpdateLocalSeat(std::uint64_t localNetworkId, SDK::EntityId localEntityId);

        /** Tells the server how far the local player's own climb has got; see the definition. */
        void ReportStage(Cart &cart, std::uint8_t seat, SDK::wh::entitymodule::E_CartActorState::Type state, MafiaNet::Time now);

        /** Reports the local player's own dismount, which only their game decides. */
        void ReportLocalLeave();

        void SendSeatRequest(std::uint64_t cartNetworkId, Shared::RPC::CartSeatRequest::Kind kind, std::uint8_t seat = 0);

        /** What the engine last said about one body in a cart. */
        struct CartStay {
            SDK::wh::entitymodule::C_ActorActionCart *action     = nullptr;
            SDK::EntityId cartEntityId                           = 0;
            SDK::wh::entitymodule::E_CartPosition::Type position = SDK::wh::entitymodule::E_CartPosition::Count;
            SDK::wh::entitymodule::E_CartActorState::Type state  = SDK::wh::entitymodule::E_CartActorState::None;
        };

        /** Whether the other back seat's climb is not yet past the middle, for a climb at back seat `seat`; see the definition. */
        bool BackClimbBlocked(const Cart *cart, std::uint8_t seat, SDK::EntityId actorEntityId) const;

        /** A copy of one body's stay, or false when it is in no cart. */
        bool ReadStay(SDK::EntityId actorEntityId, CartStay &out) const;

        /** Holds the view on the animation through the local player's own climb down from a rail place the player has no climb of; see the definition. */
        void FollowLocalClimbDown(const CartStay *localStay);

        /** Takes the local player's dismount key away while their climb down would meet another's; see the definition. */
        void HoldLocalClimbDown(const CartStay *localStay, SDK::EntityId localEntityId);

        mutable std::mutex _staysMutex;
        std::unordered_map<SDK::EntityId, CartStay> _stays;

        // The local player's body, published by the tick for the hook, which
        // may run off the game thread and cannot ask the game itself.
        std::atomic<SDK::EntityId> _localEntityId {0};

        // The animated-camera request taken for the local player's climb
        // down, and whether it is still held.
        struct ClimbCamera {
            std::uint32_t request = 0;
            MafiaNet::Time since  = 0;
            bool held             = true;
        };
        std::optional<ClimbCamera> _climbCamera;

        // Whether this module holds one count of the local player's dismount key.
        bool _climbDownHeld = false;

        // The local player's dismount edges, latched by the hook, sent by the tick.
        std::atomic<bool> _localLeft {false};
        std::atomic<SDK::EntityId> _localLeftCart {0};

        // The carts this mod built, and the other players' bodies in them,
        // for the hooks' any-thread questions.
        mutable std::mutex _cartsMutex;
        std::vector<const SDK::wh::entitymodule::C_Cart *> _modCarts;
        std::vector<const SDK::wh::entitymodule::S_CartPathCursor *> _modCursors;

        /**
         * A driver's hands on the reins, per body: the clip on, a flick until
         * when, the next keep-alive, and whether the cart was last seen
         * rolling.
         */
        struct DriverPose {
            std::string_view clip;
            MafiaNet::Time clipUntil = 0;
            MafiaNet::Time nextCheck = 0;
            std::optional<bool> moving;
        };
        std::unordered_map<SDK::EntityId, DriverPose> _driverPoses;

        // The local player's climb: when it was started and when to try again
        // if it never took, and the seat request in flight.
        MafiaNet::Time _nextMountAttempt = 0;
        MafiaNet::Time _mountStartedAt   = 0;
        std::uint64_t _requestedCart     = 0;
        MafiaNet::Time _requestedAt      = 0;

        // The seat's position for the length of a MountPlayer this module
        // runs, so the prompt hook lets it through and the action takes it.
        std::optional<SDK::wh::entitymodule::E_CartPosition::Type> _mountPosition;

        // The cart the local player climbed out of on their own, until the server's seat for them there is gone.
        std::uint64_t _leftCartNetworkId = 0;

        // When the local player's climb was last reported, so a report is not sent every frame until its bit comes back.
        MafiaNet::Time _nextStageReport = 0;

        // Per body: when the seat was last asked for, so a refusal is not re-asked every frame.
        std::unordered_map<SDK::EntityId, MafiaNet::Time> _seatAttempts;

        // When the last frame ran, for the drive's step.
        MafiaNet::Time _lastFrame = 0;
    };
} // namespace KCDC::Features::Cart
