#include "c_actor.h"

#include "../../cryengine/action/i_actor_system.h"
#include "../combatmodule/c_combat_actor.h"
#include "c_action_actor.h"
#include "c_actor_action_rider.h"
#include "c_horse.h"
#include "c_horse_ride_context.h"
#include "c_item_attachment_manager.h"
#include "c_rider_expansion.h"
#include "s_actor_class.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    void C_Actor::RefreshClothingSupport() {
        hook::this_call<void>(gPatterns.C_Actor__RefreshClothingSupport, this);
    }

    void C_Actor::EnsureEquipmentHandler() {
        if (GetSoul() == nullptr || m_pEquipmentHandler != nullptr) {
            return;
        }

        hook::this_call<void>(gPatterns.C_Actor__CreateEquipmentHandler, this);
    }

    void C_Actor::SetPutAway(bool putAway) {
        hook::this_call<void>(gPatterns.C_Actor__SetPutAway, this, putAway);
    }

    I_ActorCarryCorpse *C_Actor::GetCarryCorpse() {
        return hook::this_call<I_ActorCarryCorpse *>(gPatterns.C_Actor__GetCarryCorpse, this);
    }

    void C_Actor::RequestPutAway(bool putAway, bool immediate) {
        hook::this_call<void>(gPatterns.C_Actor__RequestPutAway, this, putAway, immediate);
    }

    // A free function taking the actor. The pointer stays opaque: what
    // matters to a caller is presence, because the engine's own hand/put-away
    // code branches on it -- except AddItemToHand's torch branch, which
    // dereferences it blind.
    void *C_Actor::GetAnimationProxy() {
        return hook::call<void *>(gPatterns.GetActorAnimationProxy, this);
    }

    // Reaches through the actor model at +0x990 without checking it, so the
    // model has to exist before the call is made.
    bool C_Actor::IsJumping() const {
        return IsSettled() && hook::this_call<bool>(gPatterns.C_Actor__IsJumping, this);
    }

    // A free function, not a member: the first argument is dead -- the game's
    // own call site passes a literal zero -- and the actor arrives in RDX.
    // The fourth argument filters the ledge id's high half, which 0xFFFF
    // turns off; the third and fifth are unused. Spelled as the engine
    // spells them rather than trimmed to what the body reads.
    bool C_Actor::TryLedgeGrab(const S_LedgeGrabInput &input) {
        S_LedgeGrabInput request = input;
        return IsSettled() && hook::call<bool>(gPatterns.TryLedgeGrab, nullptr, this, this, static_cast<std::uint16_t>(0xFFFF), &request);
    }

    C_RiderExpansion *C_Actor::GetRiderExpansion() const {
        auto *queue = GetActionActor();
        return queue != nullptr ? hook::call<C_RiderExpansion *>(gPatterns.C_ActionActor__GetRiderExpansion, queue) : nullptr;
    }

    // Reference counting mirrors the engine's own mount: the factory hands
    // back one, the queue is given its own through the handle, and the
    // factory's is dropped afterwards whether or not the queue took it.
    bool C_Actor::EnsureLocomotionAction(IActionController *controller) {
        // The native reads the action through the holder without asking
        // whether there is one.
        if (m_pAIAnimationState == nullptr || controller == nullptr) {
            return false;
        }
        return hook::call<bool>(gPatterns.C_Actor__EnsureLocomotionAction, m_pAIAnimationState, GetEntityId(), controller, m_locomotionParams);
    }

    bool C_Actor::RideHorse(EntityId horseEntityId, bool instant) {
        auto *queue = GetActionActor();
        if (queue == nullptr || horseEntityId == 0 || !IsSettled()) {
            return false;
        }

        // Asked of the animal directly, since StartRide is not on this path:
        // the engine's own check reads this very field to turn down a horse
        // that already carries somebody.
        auto *actorSystem = IActorSystem::GetInstance();
        auto *target      = static_cast<C_Actor *>(actorSystem != nullptr ? actorSystem->GetActor(horseEntityId) : nullptr);
        if (target == nullptr || !target->IsSettled() || target->GetActorClass() != C_Horse::ActorClassId) {
            return false;
        }
        if (static_cast<C_Horse *>(target)->HasRider()) {
            return false;
        }

        // Starts null and is filled in later, and the factory dereferences it
        // without asking.
        void *allocator = queue->GetActionAllocator();
        if (allocator == nullptr) {
            return false;
        }

        const std::uint64_t params = C_ActorActionRider::MakeParams(horseEntityId, instant);

        C_ActorActionRider *action = nullptr;
        hook::call<void *>(gPatterns.C_ActorActionRider__Create, allocator, &action, &params);
        if (action == nullptr) {
            return false;
        }

        action->AddRef();
        wh::framework::I_Action *handle = action;
        const bool queued = queue->QueueAction(&handle);
        action->Release();
        return queued;
    }

    // Straight through the game's own facade, which is what Lua's
    // `human:ForceDismount()` reaches: slot 205 resolves the rider expansion
    // itself and calls `StopRide(force, 0, 0, place)`. Going through the
    // expansion by hand was the same three arguments with an extra lookup to
    // get wrong.
    bool C_Actor::StopRiding(bool immediate) {
        return DismountHorse(immediate, /*place=*/true);
    }

    bool C_Actor::BeginGetOff(std::uint8_t side) {
        C_RiderExpansion *rider = GetRiderExpansion();
        return rider != nullptr && rider->StopRide(/*force=*/false, /*reason=*/true, side, /*place=*/true);
    }

    C_HorseRideContext *C_Actor::GetRideContext() const {
        auto *queue      = GetActionActor();
        auto *rideAction = queue != nullptr ? queue->FindRiderAction() : nullptr;
        return rideAction != nullptr ? rideAction->GetRideContext() : nullptr;
    }

    C_HorseRideContext::RideState C_Actor::GetRideState() const {
        auto *rideContext = GetRideContext();
        return rideContext != nullptr ? rideContext->GetState() : C_HorseRideContext::RIDE_STATE_NONE;
    }

    // Asked of the victim rather than the horse, which is how the game's own
    // bindings take it: the pull-down knows how to find what they are riding.
    bool C_Actor::CanPullDownRider(EntityId victimEntityId) {
        auto *pullDown = m_pCombatActor != nullptr ? m_pCombatActor->GetHorsePullDown() : nullptr;
        return pullDown != nullptr && pullDown->CanPullDown(victimEntityId);
    }

    bool C_Actor::RequestPullDownRider(EntityId victimEntityId) {
        auto *pullDown = m_pCombatActor != nullptr ? m_pCombatActor->GetHorsePullDown() : nullptr;
        if (pullDown == nullptr) {
            return false;
        }

        pullDown->RequestPullDown(victimEntityId);
        return true;
    }

    wh::combatmodule::C_CombatActor *C_Actor::GetOrCreateCombatActor() {
        return hook::this_call<wh::combatmodule::C_CombatActor *>(gPatterns.C_Actor__GetOrCreateCombatActor, this);
    }

    void C_Actor::RebuildClothing() {
        hook::this_call<void>(gPatterns.C_Actor__RebuildClothing, this);
    }
} // namespace SDK::wh::entitymodule
