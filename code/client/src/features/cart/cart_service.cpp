#include "cart_service.h"

#include "core/input/native_keybinds.h"
#include "features/player/player.h"
#include "common/module_support.h"

#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/entitymodule/c_cart_player_control.h"
#include "sdk/wh/entitymodule/c_disable_player_cart_dismount.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_procedural_context_animate_camera_control_method.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>

namespace KCDC::Features::Cart {
    namespace {
        constexpr const char *kLogger = "Cart";

        /** How long a climb the local player was sent on gets before it is asked for again. */
        constexpr MafiaNet::Time kMountRetryMs = 4000;

        /** How long a seat the local player never reached is kept before it is given back. */
        constexpr MafiaNet::Time kMountGiveUpMs = 15000;

        /** How long a remote body's seat or climb-down is left to take before it is asked for again. */
        constexpr MafiaNet::Time kSeatRetryMs = 2000;

        /**
         * How long a copy is left between being wanted in a seat and being put
         * there: one tag pass at least, so the `player` tag its seat's
         * animations need is on the body before the first of them is chosen.
         */
        constexpr MafiaNet::Time kFirstSeatDelayMs = 250;

        /**
         * A driver's hands on the reins: the layer they go on, over the
         * bench's own seated pose; how often the idle is started again, which
         * brings back one that stopped (a start while it still plays is
         * refused); the blend into the idle and into a flick; and the pace
         * below which the reins are held still.
         */
        constexpr std::int32_t kDriverPoseLayer     = 6;
        constexpr MafiaNet::Time kDriverPoseCheckMs = 1000;
        constexpr float kDriverPoseBlend            = 0.3F;
        constexpr float kDriverFlickBlend           = 0.2F;
        constexpr float kReinsMovingSpeed           = 0.4F;

        /** How long a seat request is left unanswered before the prompt may ask again. */
        constexpr MafiaNet::Time kRequestRetryMs = 1000;

        /** How long a climb report is left unanswered before it is sent again. */
        constexpr MafiaNet::Time kStageReportRetryMs = 1000;

        /** Whether a body's stay has reached the seat, rather than walking to it or climbing off. */
        bool IsSeatedState(SDK::wh::entitymodule::E_CartActorState::Type state) {
            return state == SDK::wh::entitymodule::E_CartActorState::Sitting || state == SDK::wh::entitymodule::E_CartActorState::RiensGo || state == SDK::wh::entitymodule::E_CartActorState::RiensStop;
        }

        /** Whether a body's stay is climbing out. */
        bool IsClimbDownState(SDK::wh::entitymodule::E_CartActorState::Type state) {
            return state == SDK::wh::entitymodule::E_CartActorState::Dismounting || state == SDK::wh::entitymodule::E_CartActorState::FastDismounting;
        }

        /** Whether a position climbs through the back of the cart, the way both back seats' climbs go. */
        bool ClimbsThroughBack(SDK::wh::entitymodule::E_CartPosition::Type position) {
            return position == SDK::wh::entitymodule::E_CartPosition::LeftBack || position == SDK::wh::entitymodule::E_CartPosition::RightBack;
        }

        /** How long the view takes to go onto the climb's animation and back, as the player's own climbs blend it. */
        constexpr float kClimbCameraBlend = 0.3F;

        /**
         * How long the view stays on a climb down: the player's own climb down
         * gives it back at 2 s (`AnimateCamera`'s exit in `WagonOut`), before
         * the climb ends and the controls come back -- given back after that,
         * it turns the view onto the climb's last direction and the controls
         * turn it back.
         */
        constexpr MafiaNet::Time kClimbCameraHoldMs = 2000;
    } // namespace

    CartService &CartService::Get() {
        static CartService instance;
        return instance;
    }

    Shared::Game::CartControls CartService::ReadControls() {
        const KCDC::Core::NativeKeybinds &keys = KCDC::Core::NativeKeybinds::Get();

        Shared::Game::CartControls controls;
        controls.throttle = static_cast<std::int8_t>((keys.IsHeld(KCDC::Core::ModKeybind::CartForward) ? 1 : 0) - (keys.IsHeld(KCDC::Core::ModKeybind::CartBack) ? 1 : 0));
        controls.steer    = (keys.IsHeld(KCDC::Core::ModKeybind::CartLeft) ? 1.0F : 0.0F) - (keys.IsHeld(KCDC::Core::ModKeybind::CartRight) ? 1.0F : 0.0F);
        controls.fast     = keys.IsHeld(KCDC::Core::ModKeybind::CartTrot);
        return controls;
    }

    void CartService::Update() {
        // The drive's step: the time since the last frame, as the game ran it.
        const MafiaNet::Time now = MafiaNet::GetTime();
        const float frameTime    = _lastFrame != 0 ? std::clamp(static_cast<float>(now - _lastFrame) / 1000.0F, 0.0F, 0.1F) : 0.0F;
        _lastFrame               = now;

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        KCDC::Features::Player::Human *local = KCDC::Features::Player::Human::FindLocal();
        const std::uint64_t localNetworkId   = local != nullptr ? local->GetNetworkID() : 0;
        const SDK::EntityId localEntityId    = KCDC::Common::LocalActorEntityId();
        _localEntityId.store(localEntityId, std::memory_order_release);
        PruneStays();

        CartStay localStay;
        const bool localInCart                    = localEntityId != 0 && ReadStay(localEntityId, localStay);
        const Shared::Game::CartControls controls = ReadControls();

        replication->ForEach<Cart>([&](Cart *cart) {
            const bool driving = localInCart && localNetworkId != 0 && cart->Driver() == localNetworkId && localStay.cartEntityId == cart->GetSeatEntityId(Shared::Entities::CartEntity::kSeatDriver) && IsSeatedState(localStay.state);
            cart->Frame(controls, driving, frameTime);
        });

        PublishCarts();
        UpdateRetired();
        UpdateSeats();
        ReportLocalLeave();
        UpdateLocalSeat(localNetworkId, localEntityId);
        FollowLocalClimbDown(localInCart ? &localStay : nullptr);
        HoldLocalClimbDown(localInCart ? &localStay : nullptr, localEntityId);
        UpdateDriverPoses();
    }

    /**
     * Forgets the stays of bodies that are gone. The register is written from
     * the one native every seat change goes through, and `Exit` does give a
     * seat back through it (0x1828D7DD4) -- but a body removed from the world
     * outright, a copy streamed out while seated, is not promised to exit
     * its action first, and a stay kept past its body would hand the tick a
     * freed action to climb down.
     */
    void CartService::PruneStays() {
        std::scoped_lock lock(_staysMutex);
        for (auto stay = _stays.begin(); stay != _stays.end();) {
            stay = KCDC::Common::ResolveEntity(stay->first) == nullptr ? _stays.erase(stay) : std::next(stay);
        }
    }

    bool CartService::HasOccupants(const std::vector<SDK::EntityId> &cartEntityIds) const {
        std::scoped_lock lock(_staysMutex);
        for (const auto &[entityId, stay] : _stays) {
            if (std::find(cartEntityIds.begin(), cartEntityIds.end(), stay.cartEntityId) != cartEntityIds.end()) {
                return true;
            }
        }
        return false;
    }

    void CartService::RemoveCartEntities(const std::vector<SDK::EntityId> &entities) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return;
        }
        for (SDK::EntityId entityId : entities) {
            entitySystem->RemoveEntity(entityId, false);
        }
    }

    void CartService::Retire(std::vector<SDK::EntityId> cartEntityIds, std::vector<SDK::EntityId> entities) {
        if (!HasOccupants(cartEntityIds)) {
            RemoveCartEntities(entities);
            return;
        }

        // Out of sight at once, horses and all, and everyone in it down the
        // quick way.
        for (SDK::EntityId entityId : entities) {
            if (SDK::IEntity *entity = KCDC::Common::ResolveEntity(entityId)) {
                entity->Hide(true);
            }
        }
        std::vector<SDK::wh::entitymodule::C_ActorActionCart *> actions;
        {
            std::scoped_lock lock(_staysMutex);
            for (const auto &[entityId, stay] : _stays) {
                if (std::find(cartEntityIds.begin(), cartEntityIds.end(), stay.cartEntityId) != cartEntityIds.end() && stay.action != nullptr) {
                    actions.push_back(stay.action);
                }
            }
        }
        for (SDK::wh::entitymodule::C_ActorActionCart *action : actions) {
            action->RequestDismount(true);
        }

        _retired.push_back(RetiredCart {std::move(cartEntityIds), std::move(entities), MafiaNet::GetTime() + 10000});
        Framework::Logging::GetLogger(kLogger)->info("A cart going away still has {} body(ies) in it; getting them out first", actions.size());
    }

    void CartService::UpdateRetired() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        for (auto retired = _retired.begin(); retired != _retired.end();) {
            if (HasOccupants(retired->cartEntityIds) && now < retired->deadline) {
                ++retired;
                continue;
            }
            RemoveCartEntities(retired->entities);
            retired = _retired.erase(retired);
        }
    }

    void CartService::Reset() {
        // The world is going with the session; nothing is left to wait for.
        for (const RetiredCart &retired : _retired) {
            RemoveCartEntities(retired.entities);
        }
        _retired.clear();
        {
            std::scoped_lock lock(_staysMutex);
            _stays.clear();
        }
        {
            std::scoped_lock lock(_cartsMutex);
            _modCarts.clear();
            _modCursors.clear();
        }
        _driverPoses.clear();
        _localLeft.store(false, std::memory_order_release);
        _localLeftCart.store(0, std::memory_order_release);
        _mountStartedAt    = 0;
        _nextMountAttempt  = 0;
        _requestedCart     = 0;
        _requestedAt       = 0;
        _leftCartNetworkId = 0;
        _climbCamera.reset();
        if (_climbDownHeld && SDK::wh::entitymodule::C_Player::GetInstance() != nullptr) {
            SDK::wh::entitymodule::C_DisablePlayerCartDismount::SetDismountDisabled(false);
        }
        _climbDownHeld     = false;
        _lastFrame         = 0;
        _seatAttempts.clear();
    }

    // --- Questions the hooks ask ------------------------------------------

    Cart *CartService::FindByCartEntity(SDK::EntityId cartEntityId) const {
        if (cartEntityId == 0) {
            return nullptr;
        }
        return KCDC::Common::FindReplica<Cart>([cartEntityId](Cart *cart) {
            return cart->GetCartEntityId() == cartEntityId || cart->SeatOfEntity(cartEntityId).has_value();
        });
    }

    bool CartService::IsModCart(const SDK::wh::entitymodule::C_Cart *cart) const {
        std::scoped_lock lock(_cartsMutex);
        return cart != nullptr && std::find(_modCarts.begin(), _modCarts.end(), cart) != _modCarts.end();
    }

    bool CartService::IsModCursor(const SDK::wh::entitymodule::S_CartPathCursor *cursor) const {
        std::scoped_lock lock(_cartsMutex);
        return cursor != nullptr && std::find(_modCursors.begin(), _modCursors.end(), cursor) != _modCursors.end();
    }

    void CartService::PublishCarts() {
        std::vector<const SDK::wh::entitymodule::C_Cart *> carts;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Cart>([&](Cart *cart) {
                cart->CollectNativeCarts(carts);
            });
        }
        std::scoped_lock lock(_cartsMutex);
        _modCarts = std::move(carts);
    }

    // --- The engine's seat edges ------------------------------------------

    void CartService::NotifyCartState(SDK::wh::entitymodule::C_ActorActionCart *action, SDK::wh::entitymodule::E_CartActorState::Type state) {
        SDK::wh::entitymodule::C_ActionActor *actionActor = action != nullptr ? action->GetActionActor() : nullptr;
        SDK::wh::entitymodule::C_Actor *actor             = actionActor != nullptr ? actionActor->GetActor() : nullptr;
        if (actor == nullptr) {
            return;
        }

        const SDK::EntityId actorEntityId = actor->GetEntityId();
        std::scoped_lock lock(_staysMutex);
        if (state == SDK::wh::entitymodule::E_CartActorState::None) {
            const auto found = _stays.find(actorEntityId);
            if (found != _stays.end() && actorEntityId == _localEntityId.load(std::memory_order_acquire)) {
                // The local player is out. Only their own game decides that
                // for them, so it is what the server is told -- from the tick.
                _localLeftCart.store(found->second.cartEntityId, std::memory_order_release);
                _localLeft.store(true, std::memory_order_release);
            }
            _stays.erase(actorEntityId);
            return;
        }

        CartStay &stay    = _stays[actorEntityId];
        stay.action       = action;
        stay.cartEntityId = action->GetCartEntityId();
        stay.position     = action->GetPosition();
        stay.state        = state;
    }

    /**
     * The player's own climb down carries the `AnimateCamera` clip, which
     * holds the view on the animation; the climbs down from the other places
     * along the rails are the game's NPC ones, which do not. The local player's
     * climb down from such a seat takes the same request on the same context
     * the clip would, and gives it back when the climb is over.
     */
    void CartService::FollowLocalClimbDown(const CartStay *localStay) {
        const bool climbing      = localStay != nullptr && IsClimbDownState(localStay->state) && localStay->position != SDK::wh::entitymodule::E_CartPosition::RightBack && localStay->position != SDK::wh::entitymodule::E_CartPosition::Codriver;
        const MafiaNet::Time now = MafiaNet::GetTime();
        const bool hold          = climbing && (!_climbCamera || now - _climbCamera->since < kClimbCameraHoldMs);
        if (!climbing && !_climbCamera) {
            return;
        }
        if (_climbCamera && hold == _climbCamera->held) {
            if (!climbing) {
                _climbCamera.reset();
            }
            return;
        }
        SDK::wh::entitymodule::C_Actor *actor = KCDC::Common::LocalHumanActor();
        SDK::CAnimatedCharacter *animated     = actor != nullptr ? actor->GetAnimatedCharacterRuntime() : nullptr;
        SDK::IActionController *controller    = animated != nullptr ? animated->GetActionController() : nullptr;
        if (controller == nullptr) {
            _climbCamera.reset();
            return;
        }
        auto *camera = reinterpret_cast<SDK::wh::entitymodule::C_ProceduralContextAnimateCameraControlMethod *>(controller->FindOrCreateProceduralContext("ProceduralContextACCM"));
        if (!_climbCamera) {
            _climbCamera = ClimbCamera {camera->RequestAnimatedCamera(0.0F, 0.0F, kClimbCameraBlend), now, true};
            return;
        }
        camera->ReleaseAnimatedCamera(_climbCamera->request, kClimbCameraBlend, actor);
        _climbCamera->held = false;
        if (!climbing) {
            _climbCamera.reset();
        }
    }

    /**
     * The game takes its passengers in and out one at a time (the NPC cart
     * queue, `C_CartActionOut::Update`, 0x183340834). The two back seats climb
     * through the back of the cart and meet there; every other climb has a
     * way of its own. So a back seat's climb waits for the other back seat's
     * to be past the middle between the two seats: one climbing down still
     * nearer its seat than that, or one climbing in still farther from it.
     * The local player's climb in waits, and their dismount key is taken
     * away -- each player is held on their own machine, so every copy
     * follows its owner's timing and all of them see one order.
     */
    bool CartService::BackClimbBlocked(const Cart *cart, std::uint8_t seat, SDK::EntityId actorEntityId) const {
        const std::optional<glm::vec3> own = ClimbsThroughBack(Cart::SeatPosition(*cart->Seats().At(seat))) ? cart->SeatWorldPoint(seat) : std::nullopt;
        if (!own) {
            return false;
        }
        std::scoped_lock lock(_staysMutex);
        for (const auto &[entityId, stay] : _stays) {
            const bool climbingIn = stay.state == SDK::wh::entitymodule::E_CartActorState::Mounting;
            if (entityId == actorEntityId || !ClimbsThroughBack(stay.position) || !(climbingIn || IsClimbDownState(stay.state))) {
                continue;
            }
            const std::optional<std::uint8_t> other = cart->SeatOfEntity(stay.cartEntityId);
            const std::optional<glm::vec3> otherAt  = other ? cart->SeatWorldPoint(*other) : std::nullopt;
            if (!otherAt) {
                continue;
            }
            const float middle   = glm::length(glm::vec2(*own - *otherAt)) * 0.5F;
            const float fromSeat = glm::length(glm::vec2(KCDC::Common::ToGlm(KCDC::Common::ResolveEntity(entityId)->GetWorldPos()) - *otherAt));
            if (climbingIn ? fromSeat > middle : fromSeat < middle) {
                return true;
            }
        }
        return false;
    }

    /** The local player's dismount key, taken away while their climb down would meet another's; see `BackClimbBlocked`. */
    void CartService::HoldLocalClimbDown(const CartStay *localStay, SDK::EntityId localEntityId) {
        Cart *cart      = localStay != nullptr && IsSeatedState(localStay->state) ? FindByCartEntity(localStay->cartEntityId) : nullptr;
        const bool held = cart != nullptr && BackClimbBlocked(cart, *cart->SeatOfEntity(localStay->cartEntityId), localEntityId);
        if (held != _climbDownHeld) {
            SDK::wh::entitymodule::C_DisablePlayerCartDismount::SetDismountDisabled(held);
            _climbDownHeld = held;
        }
    }

    bool CartService::IsInCart(SDK::EntityId actorEntityId) const {
        std::scoped_lock lock(_staysMutex);
        return actorEntityId != 0 && _stays.contains(actorEntityId);
    }

    bool CartService::ReadStay(SDK::EntityId actorEntityId, CartStay &out) const {
        std::scoped_lock lock(_staysMutex);
        const auto found = _stays.find(actorEntityId);
        if (found == _stays.end()) {
            return false;
        }
        out = found->second;
        return true;
    }

    // --- The local player's mount -----------------------------------------

    bool CartService::InterceptPromptMount(SDK::EntityId cartEntityId, bool frontSeat) {
        if (_mountPosition) {
            return false;
        }
        Cart *cart = FindByCartEntity(cartEntityId);
        if (cart == nullptr) {
            // A cart the level placed is the game's own business.
            return false;
        }
        // The game's prompt refuses a moving cart by its own movement
        // (`CanPlayerMount`, `!C_Cart::IsMoving`), which a driven cart never
        // uses: it is refused here instead.
        if (cart->IsRolling()) {
            return true;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_requestedCart != cart->GetNetworkID() || now - _requestedAt > kRequestRetryMs) {
            _requestedCart = cart->GetNetworkID();
            _requestedAt   = now;
            SendSeatRequest(cart->GetNetworkID(), frontSeat ? Shared::RPC::CartSeatRequest::Kind::Front : Shared::RPC::CartSeatRequest::Kind::Back);
        }
        return true;
    }

    void CartService::SendSeatRequest(std::uint64_t cartNetworkId, Shared::RPC::CartSeatRequest::Kind kind, std::uint8_t seat) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::CartSeatRequest request;
        request.cartNetworkId = cartNetworkId;
        request.kind          = static_cast<std::uint8_t>(kind);
        request.seat          = seat;
        net->BroadcastRPC(request);
    }

    /**
     * Climbs the local player into the seat the server gave them, through the
     * game's own mount -- the walk to the cart, the climb, the camera, the
     * dismount key -- or out of one it took away, and tells the server how
     * far the climb has got, which every other client's copy follows.
     */
    void CartService::UpdateLocalSeat(std::uint64_t localNetworkId, SDK::EntityId localEntityId) {
        if (localNetworkId == 0 || localEntityId == 0) {
            return;
        }

        Cart *assigned   = nullptr;
        std::size_t seat = Shared::Entities::CartEntity::kSeatCount;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Cart>([&](Cart *cart) {
                const std::size_t at = cart->SeatOf(localNetworkId);
                if (assigned == nullptr && at < Shared::Entities::CartEntity::kSeatCount) {
                    assigned = cart;
                    seat     = at;
                }
            });
        }

        CartStay stay;
        const bool inCart        = ReadStay(localEntityId, stay);
        const MafiaNet::Time now = MafiaNet::GetTime();

        if (assigned == nullptr || assigned->GetNetworkID() != _leftCartNetworkId) {
            _leftCartNetworkId = 0;
        }
        if (assigned == nullptr || assigned->GetCart() == nullptr) {
            _mountStartedAt = 0;
            // In a cart of ours the server no longer seats us in: out, the way
            // the dismount key does it.
            if (inCart && stay.action != nullptr && FindByCartEntity(stay.cartEntityId) != nullptr && stay.state != SDK::wh::entitymodule::E_CartActorState::Dismounting && stay.state != SDK::wh::entitymodule::E_CartActorState::FastDismounting && now >= _nextMountAttempt) {
                _nextMountAttempt = now + kMountRetryMs;
                stay.action->RequestDismount(false);
                Framework::Logging::GetLogger(kLogger)->info("The server has the local player out of their cart; climbing down");
            }
            return;
        }

        // A seat we climbed out of ourselves stays ours on the server until
        // our Leave has reached it; it is not climbed back into meanwhile.
        if (assigned->GetNetworkID() == _leftCartNetworkId) {
            return;
        }

        const Shared::Game::CartSeat *wanted = assigned->Seats().At(seat);
        const SDK::EntityId seatEntityId     = assigned->GetSeatEntityId(static_cast<std::uint8_t>(seat));
        if (wanted == nullptr || seatEntityId == 0) {
            return;
        }
        const auto nativeSeat = Cart::SeatPosition(*wanted);
        if (inCart && stay.cartEntityId == seatEntityId) {
            _mountStartedAt = 0;
            ReportStage(*assigned, static_cast<std::uint8_t>(seat), stay.state, now);
            return;
        }
        if (inCart && stay.action != nullptr && stay.state != SDK::wh::entitymodule::E_CartActorState::Dismounting && stay.state != SDK::wh::entitymodule::E_CartActorState::FastDismounting && now >= _nextMountAttempt) {
            // Moved to another seat, or another cart: down from the old one
            // first, the way the dismount key does it, then up into the new.
            _nextMountAttempt = now + kMountRetryMs;
            stay.action->RequestDismount(false);
            Framework::Logging::GetLogger(kLogger)->info("The local player climbs down to take seat {} of cart {}", wanted->name, assigned->GetNetworkID());
            return;
        }
        if (inCart || now < _nextMountAttempt) {
            return;
        }

        if (_mountStartedAt == 0) {
            _mountStartedAt = now;
        }
        else if (now - _mountStartedAt > kMountGiveUpMs) {
            // The climb never took -- blocked, or the game would not have it.
            // The seat goes back rather than being held by nobody.
            Framework::Logging::GetLogger(kLogger)->warn("The local player never reached seat {} of cart {}; giving it back", seat, assigned->GetNetworkID());
            _mountStartedAt = 0;
            SendSeatRequest(assigned->GetNetworkID(), Shared::RPC::CartSeatRequest::Kind::Leave);
            _nextMountAttempt = now + kMountRetryMs;
            return;
        }

        auto *player  = SDK::wh::entitymodule::C_Player::GetInstance();
        auto *control = player != nullptr ? player->GetCartPlayerControl() : nullptr;
        if (control == nullptr || BackClimbBlocked(assigned, static_cast<std::uint8_t>(seat), localEntityId)) {
            return;
        }

        _nextMountAttempt = now + kMountRetryMs;
        _mountPosition    = nativeSeat;
        control->MountPlayer(seatEntityId, wanted->front);
        _mountPosition.reset();

        // The action's `Enter` runs inside the queue's insert and states the
        // stay at once (0x1828D7614), so no stay now is the game refusing:
        // a cart it takes for moving, or a seat it will not give.
        CartStay entered;
        if (ReadStay(localEntityId, entered)) {
            Framework::Logging::GetLogger(kLogger)->info("Climbing the local player into seat {} of cart {}", wanted->name, assigned->GetNetworkID());
        }
        else {
            SDK::wh::entitymodule::C_Cart *native = SDK::wh::entitymodule::C_Cart::FindByEntityId(seatEntityId);
            Framework::Logging::GetLogger(kLogger)->warn("The game would not climb the local player into seat {} of cart {}: seat enabled {}, held by entity {}", wanted->name, assigned->GetNetworkID(), native != nullptr && native->IsPositionEnabled(nativeSeat),
                native != nullptr ? native->GetActorAtPosition(nativeSeat) : 0);
        }
    }

    /**
     * The local player's climb, as far as it has got: in (`Mounting`), sat
     * (the seat's idle), and down (`Dismounting`). Each is sent once its bit
     * is not yet on the wire, and again after a while if it never comes.
     */
    void CartService::ReportStage(Cart &cart, std::uint8_t seat, SDK::wh::entitymodule::E_CartActorState::Type state, MafiaNet::Time now) {
        Shared::RPC::CartSeatRequest::Kind stage = Shared::RPC::CartSeatRequest::Kind::Leave;
        if ((state == SDK::wh::entitymodule::E_CartActorState::Dismounting || state == SDK::wh::entitymodule::E_CartActorState::FastDismounting) && !Shared::Entities::CartEntity::HasSeatBit(cart.leaving, seat)) {
            stage = Shared::RPC::CartSeatRequest::Kind::Leaving;
        }
        else if (IsSeatedState(state) && !Shared::Entities::CartEntity::HasSeatBit(cart.seated, seat)) {
            stage = Shared::RPC::CartSeatRequest::Kind::Seated;
        }
        else if (state == SDK::wh::entitymodule::E_CartActorState::Mounting && !Shared::Entities::CartEntity::HasSeatBit(cart.boarded, seat)) {
            stage = Shared::RPC::CartSeatRequest::Kind::Boarding;
        }
        if (stage == Shared::RPC::CartSeatRequest::Kind::Leave || now < _nextStageReport) {
            return;
        }
        _nextStageReport = now + kStageReportRetryMs;
        SendSeatRequest(cart.GetNetworkID(), stage, seat);
    }

    void CartService::ReportLocalLeave() {
        if (!_localLeft.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        Cart *cart                           = FindByCartEntity(_localLeftCart.load(std::memory_order_acquire));
        KCDC::Features::Player::Human *local = KCDC::Features::Player::Human::FindLocal();
        if (cart == nullptr || local == nullptr || cart->SeatOf(local->GetNetworkID()) >= Shared::Entities::CartEntity::kSeatCount) {
            return;
        }
        // Our own climb down, or a climb that fell through: either way the
        // server still has us in the seat, and only we can say we are out.
        SendSeatRequest(cart->GetNetworkID(), Shared::RPC::CartSeatRequest::Kind::Leave);
        _leftCartNetworkId = cart->GetNetworkID();
        Framework::Logging::GetLogger(kLogger)->info("The local player got out of cart {}", cart->GetNetworkID());
    }

    // --- Everybody in a cart ----------------------------------------------

    /**
     * A driver's hands on the reins. The game seats a driver with the bench
     * passenger's own pose, hands in the lap; the driver's idle goes over it
     * on a layer of its own, with a flick of the reins as the horses set off
     * and a pull as they stop, and comes off for the climb down and whenever
     * the body is not sitting at the reins. The local player's own, and every
     * copy's on this client alike. A start of the idle while it still plays
     * is refused, so it is started again now and then: one that had stopped
     * comes back.
     */
    void CartService::UpdateDriverPoses() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();

        // Who sits at the reins, sat down rather than climbing, and how fast their cart goes.
        std::unordered_map<SDK::EntityId, float> drivers;
        std::vector<std::pair<SDK::EntityId, CartStay>> stays;
        {
            std::scoped_lock lock(_staysMutex);
            stays.assign(_stays.begin(), _stays.end());
        }
        for (const auto &[entityId, stay] : stays) {
            Cart *cart = IsSeatedState(stay.state) ? FindByCartEntity(stay.cartEntityId) : nullptr;
            if (cart != nullptr && cart->SeatOfEntity(stay.cartEntityId) == Shared::Entities::CartEntity::kSeatDriver) {
                drivers[entityId] = cart->GetSpeed();
            }
        }

        // Off the bodies that are not any more.
        for (auto pose = _driverPoses.begin(); pose != _driverPoses.end();) {
            if (drivers.contains(pose->first)) {
                ++pose;
                continue;
            }
            SDK::IEntity *body                 = KCDC::Common::ResolveEntity(pose->first);
            SDK::ICharacterInstance *character = body != nullptr ? body->GetCharacter(0) : nullptr;
            if (SDK::ISkeletonAnim *skeleton = character != nullptr ? character->GetISkeletonAnim() : nullptr) {
                skeleton->StopAnimationInLayer(kDriverPoseLayer, kDriverPoseBlend);
            }
            pose = _driverPoses.erase(pose);
        }

        for (const auto &[entityId, speed] : drivers) {
            SDK::IEntity *body                 = KCDC::Common::ResolveEntity(entityId);
            SDK::ICharacterInstance *character = body != nullptr ? body->GetCharacter(0) : nullptr;
            SDK::ISkeletonAnim *skeleton       = character != nullptr ? character->GetISkeletonAnim() : nullptr;
            if (skeleton == nullptr) {
                continue;
            }
            DriverPose &pose  = _driverPoses[entityId];
            const bool moving = std::fabs(speed) > kReinsMovingSpeed;
            if (pose.moving && *pose.moving != moving) {
                const std::string_view flick = moving ? Shared::Game::kCartDriverGoClip : Shared::Game::kCartDriverStopClip;
                skeleton->StopAnimationInLayer(kDriverPoseLayer, 0.0F);
                const bool started = Cart::StartClip(body, flick, kDriverPoseLayer, kDriverFlickBlend, 1.0F, false);
                const float length = Cart::ClipLength(body, flick);
                pose.clip          = flick;
                pose.clipUntil     = started ? now + static_cast<MafiaNet::Time>((length > 0.2F ? length : 1.5F) * 1000.0F) : now;
            }
            pose.moving = moving;
            if (pose.clip != Shared::Game::kCartDriverClip && now < pose.clipUntil) {
                continue;
            }
            if (pose.clip != Shared::Game::kCartDriverClip) {
                skeleton->StopAnimationInLayer(kDriverPoseLayer, 0.0F);
                Cart::StartClip(body, Shared::Game::kCartDriverClip, kDriverPoseLayer, kDriverPoseBlend, 1.0F, true);
                pose.clip      = Shared::Game::kCartDriverClip;
                pose.nextCheck = now + kDriverPoseCheckMs;
            }
            else if (now >= pose.nextCheck) {
                Cart::StartClip(body, Shared::Game::kCartDriverClip, kDriverPoseLayer, kDriverPoseBlend, 1.0F, true);
                pose.nextCheck = now + kDriverPoseCheckMs;
            }
        }
    }

    /**
     * Every other player's copy is put in the seat the player sits in, by the
     * same cart action the game seats its own passengers with, and with the
     * native `player` tag its seat's animations are authored under. It climbs
     * in when the player's own climb starts --
     * the cart action from the approach's end, `WaitingForMount` then the
     * seat's own climb, warped onto the seat as the game warps it -- and down
     * when theirs does, so it is seen doing what they did, where they did it.
     * A copy that streams in to find its player already sitting is put
     * straight on the seat (`skipApproach`). Reconciled every frame rather
     * than on edges, so a body that streams in late or loses its seat to a
     * level change still ends up right.
     */
    void CartService::UpdateSeats() {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        if (replication == nullptr || actorSystem == nullptr) {
            return;
        }

        const SDK::EntityId localEntityId = _localEntityId.load(std::memory_order_acquire);
        const MafiaNet::Time now          = MafiaNet::GetTime();

        struct Wanted {
            SDK::EntityId seatEntityId                           = 0;
            SDK::wh::entitymodule::E_CartPosition::Type position = SDK::wh::entitymodule::E_CartPosition::Codriver;
            bool alreadySeated                                   = false;
        };
        std::unordered_map<SDK::EntityId, Wanted> wanted;
        std::vector<SDK::EntityId> modCartEntities;

        replication->ForEach<Cart>([&](Cart *cart) {
            if (cart->GetCart() == nullptr) {
                return;
            }
            const std::vector<SDK::EntityId> ids = cart->GetCartEntityIds();
            modCartEntities.insert(modCartEntities.end(), ids.begin(), ids.end());
            const Shared::Game::CartSeatLayout layout = cart->Seats();
            for (std::uint8_t seat = 0; seat < layout.count; ++seat) {
                // Not before their own climb has started, and no longer once
                // their climb down has.
                if (!Shared::Entities::CartEntity::HasSeatBit(cart->boarded, seat) || Shared::Entities::CartEntity::HasSeatBit(cart->leaving, seat) || cart->GetSeatEntityId(seat) == 0) {
                    continue;
                }
                auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(cart->seats[seat]);
                if (human == nullptr || human->IsOwner() || human->GetEntityId() == 0) {
                    continue;
                }
                wanted[human->GetEntityId()] = Wanted {cart->GetSeatEntityId(seat), Cart::SeatPosition(layout.seats[seat]), Shared::Entities::CartEntity::HasSeatBit(cart->seated, seat)};
            }
        });

        const auto isModCartEntity = [&modCartEntities](SDK::EntityId cartEntityId) {
            return std::find(modCartEntities.begin(), modCartEntities.end(), cartEntityId) != modCartEntities.end();
        };

        // Out of seats the wire no longer gives them.
        std::vector<std::pair<SDK::EntityId, CartStay>> stays;
        {
            std::scoped_lock lock(_staysMutex);
            stays.assign(_stays.begin(), _stays.end());
        }
        for (const auto &[entityId, stay] : stays) {
            if (entityId == localEntityId || stay.action == nullptr || !isModCartEntity(stay.cartEntityId)) {
                continue;
            }
            const auto want = wanted.find(entityId);
            if (want != wanted.end() && want->second.seatEntityId == stay.cartEntityId) {
                continue;
            }
            if (IsClimbDownState(stay.state)) {
                continue;
            }
            MafiaNet::Time &attempt = _seatAttempts[entityId];
            if (now >= attempt) {
                attempt = now + kSeatRetryMs;
                stay.action->RequestDismount(false);
            }
        }

        // Into the seats it does.
        for (const auto &[entityId, want] : wanted) {
            CartStay stay;
            if (ReadStay(entityId, stay)) {
                continue;
            }

            MafiaNet::Time &attempt = _seatAttempts[entityId];
            if (now < attempt) {
                continue;
            }
            attempt = now + kSeatRetryMs;

            auto *actor         = static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(entityId));
            auto *actionActor   = actor != nullptr ? actor->GetActionActor() : nullptr;
            auto *stateExpander = actionActor != nullptr ? actionActor->GetActorStateExpansion() : nullptr;
            if (stateExpander == nullptr || !actor->IsSettled()) {
                continue;
            }

            SDK::wh::entitymodule::S_ActorActionCartParams params {};
            params.cartEntityId   = want.seatEntityId;
            params.position       = want.position;
            params.skipApproach   = want.alreadySeated;
            params.pPlayerControl = nullptr;
            stateExpander->RequestCartAction(params);
        }

        // Attempts for bodies nobody is waiting on any more.
        for (auto attempt = _seatAttempts.begin(); attempt != _seatAttempts.end();) {
            attempt = !wanted.contains(attempt->first) && !IsInCart(attempt->first) ? _seatAttempts.erase(attempt) : std::next(attempt);
        }
    }
} // namespace KCDC::Features::Cart
