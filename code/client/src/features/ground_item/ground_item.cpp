#include "ground_item.h"
#include "features/items/custom_item_module.h"
#include "ground_item_service.h"

#include "features/player/player.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_service.h"
#include "common/module_support.h"
#include "features/items/native_item_effects.h"

#include "common/vector_math.h"

#include "shared/game/missile_catalog.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/inventory_property_policy.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/entitymodule/c_inventory_manager.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_missile_weapon_runtime_data.h"
#include "sdk/wh/entitymodule/c_pickable_item.h"
#include "sdk/wh/entitymodule/c_world_inventory.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>

#include <algorithm>
#include <unordered_set>

namespace KCDC::Features::GroundItem {
    namespace {
        constexpr const char *kLogger = "GroundItem";

        /** Consecutive samples that decide a stack started or stopped moving. */
        constexpr std::uint8_t kSettleSteps = 3;

        // C_Inventory::CreateItems reports failure from a path that runs after
        // the add, so a null descriptor is not proof that nothing was created.
        // Every creation site is attempt-bounded rather than retried per frame.
        constexpr std::uint8_t kMaxMaterializeAttempts = 3;

        /** Squared slack below which two poses count as the same one. */
        constexpr float kPoseEpsilon = 0.000001F;

        /** How often a replica confirms its world stack still exists. */
        constexpr std::uint64_t kBindingCheckIntervalMs = 1000;

        /** How long a put-down prop waits for its carrier's put-down to drop the item here. */
        constexpr std::uint64_t kCarriedDropWaitMs = 6000;

        std::uint64_t NowMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        // SpawnPickable's native ABI takes a std::function, and MSVC emits an RTTI
        // type descriptor for whatever it wraps -- a lambda's descriptor is named
        // after the enclosing method, leaking it into the shipped binary. A named
        // free function makes that descriptor a generic function-pointer type.
        void NoopPickableBound(SDK::wh::entitymodule::C_PickableItem &) {}

        /** The world pose of a stack, once its pickable exists. */
        bool PickablePose(SDK::wh::entitymodule::C_Item *item, glm::vec3 &position, glm::quat &rotation) {
            auto *pickable = item != nullptr ? item->GetPickableItem() : nullptr;
            auto *entity   = pickable != nullptr ? pickable->GetEntity() : nullptr;
            if (entity == nullptr) {
                return false;
            }

            const SDK::Vec3 worldPosition = entity->GetWorldPos();
            const SDK::Quat worldRotation = entity->GetWorldRotation();
            if (!KCDC::Common::IsFinite(worldPosition, worldRotation)) {
                return false;
            }

            position = KCDC::Common::ToGlm(worldPosition);
            rotation = glm::normalize(KCDC::Common::ToGlm(worldRotation));
            return true;
        }
    } // namespace

    // --- Replica3 hooks ---------------------------------------------------

    void GroundItem::OnDeserialized(bool) {
        // Placed from Frame: every frame while it falls, once when it rests.
        _poseDirty = true;
    }

    void GroundItem::OnStateForced() {
        // The server moved the stack itself (the drop, a settle); its own
        // report echoed back is not a reason to warp.
        if (WasPoseForced()) {
            ResetSampling();
            ApplyRemotePose(true);
        }
    }

    void GroundItem::DeallocReplica(MafiaNet::Connection_RM3 *) {
        CleanupNative();
        delete this;
    }

    SDK::wh::entitymodule::C_Item *GroundItem::GetNativeItem() const {
        auto *world = WorldInventory();
        if (world == nullptr || _nativeItemWuid == 0 || _nativeItem == nullptr) {
            return nullptr;
        }
        // One lookup in the item manager, where walking the world inventory
        // was one step per item lying in the world. Still the same stack, and
        // still in the world: held there, or owned by it while lent out.
        SDK::wh::entitymodule::C_Item *current = FindItem(_nativeItemWuid);
        return current == _nativeItem && (current->GetEffectiveHolder() == world || current->GetOwner() == world) ? current : nullptr;
    }

    void GroundItem::RefreshNativeBinding() {
        if (_nativeItem == nullptr || GetNativeItem() != nullptr) {
            return;
        }
        // A confirmed binding disappearing ends that native lifetime; a new
        // one may be built. An ambiguous CreateItems result has no confirmed
        // WUID, so it stays failed instead of minting more.
        if (_nativeItemWuid != 0) {
            _materializeAttempts   = 0;
            _materializationFailed = false;
        }
        _nativeItem      = nullptr;
        _profiledItem    = nullptr;
        _nativeItemWuid  = 0;
        _createdNative   = false;
        _visualPoseKnown = false;
        ResetSampling();
    }

    void GroundItem::ResetSampling() {
        _samplePoseKnown              = false;
        _sourceConsecutiveStillSteps  = 0;
        _sourceConsecutiveMotionSteps = 0;
    }

    // --- Simulation and observer correction --------------------------------

    void GroundItem::Frame() {
        const auto now = NowMilliseconds();
        if (now >= _nextBindingCheck) {
            _nextBindingCheck = now + kBindingCheckIntervalMs;
            RefreshNativeBinding();
        }
        if (IsSimulating() != _simulating) {
            // A handover starts from the pose the replica holds, body included.
            _simulating = IsSimulating();
            ResetSampling();
            ApplyRemotePose(true);
        }
        if (_nativeItem == nullptr) {
            return;
        }
        if (_simulating) {
            Simulate();
        }
        else if (!resting) {
            ApplyRemotePose();
        }
        else if (_poseDirty) {
            ApplyRemotePose(true);
        }
    }

    void GroundItem::Simulate() {
        glm::vec3 sampledPosition;
        glm::quat sampledRotation;
        if (!PickablePose(_nativeItem, sampledPosition, sampledRotation)) {
            return;
        }
        const bool moved = !_samplePoseKnown || glm::distance(sampledPosition, _samplePosition) > 0.001F || 1.0F - std::abs(glm::dot(sampledRotation, _sampleRotation)) > kPoseEpsilon;
        _samplePosition  = sampledPosition;
        _sampleRotation  = sampledRotation;
        _samplePoseKnown = true;
        if (moved) {
            _sourceConsecutiveStillSteps  = 0;
            _sourceConsecutiveMotionSteps = std::min<std::uint8_t>(_sourceConsecutiveMotionSteps + 1, kSettleSteps);
            if (_sourceConsecutiveMotionSteps >= kSettleSteps) {
                NoteBecamePhysical();
            }
        }
        else {
            _sourceConsecutiveMotionSteps = 0;
            _sourceConsecutiveStillSteps  = std::min<std::uint8_t>(_sourceConsecutiveStillSteps + 1, kSettleSteps);
        }
        position = sampledPosition;
        rotation = sampledRotation;
        // The rest pose rides the reliable channel, so a late joiner and a new
        // simulator find the stack where it came to rest.
        const bool settled = _sourceConsecutiveStillSteps >= kSettleSteps;
        if (settled && (!resting || glm::distance(restPosition, sampledPosition) > 0.001F || 1.0F - std::abs(glm::dot(restRotation, sampledRotation)) > kPoseEpsilon)) {
            NoteBecamePhysical();
            resting      = true;
            restPosition = sampledPosition;
            restRotation = sampledRotation;
        }
        else if (!settled && resting && _sourceConsecutiveMotionSteps >= kSettleSteps) {
            resting = false;
        }
    }

    /**
     * Tells LocalEvents the stack is really on the ground now.
     *
     * Owed once, and owed even for a stack that never bounced: it is the edge
     * the held-item bookkeeping watches for.
     */
    void GroundItem::NoteBecamePhysical() {
        if (_becamePhysical || _nativeItem == nullptr || KCDC::Core::gApplication == nullptr) {
            return;
        }
        _becamePhysical = true;
        KCDC::Core::gApplication->GetLocalEvents().GroundDropBecamePhysical(_nativeItem->GetId());
    }

    // --- Observer side: build the stack and place it -----------------------

    void GroundItem::EnsureMaterialized() {
        // Also the retry for a stack whose pose arrived before its world item
        // could be built.
        if (_materializationFailed || _carriedAway || _nativeItem != nullptr) {
            return;
        }
        ApplyRemotePose(true);
    }

    bool GroundItem::IsPickableEntity(SDK::EntityId entityId) const {
        auto *pickable = _nativeItem != nullptr ? _nativeItem->GetPickableItem() : nullptr;
        auto *entity   = pickable != nullptr ? pickable->GetEntity() : nullptr;
        return entity != nullptr && entity->GetId() == entityId;
    }

    SDK::wh::entitymodule::C_Item *GroundItem::ReleaseNativeForCarry() {
        SDK::wh::entitymodule::C_Item *native = GetNativeItem();
        if (native == nullptr || native->GetPickableItem() == nullptr) {
            return nullptr;
        }
        _nativeItem      = nullptr;
        _profiledItem    = nullptr;
        _nativeItemWuid  = 0;
        _createdNative   = false;
        _carriedAway     = true;
        _visualPoseKnown = false;
        ResetSampling();
        return native;
    }

    void GroundItem::RebindNative(SDK::wh::entitymodule::C_Item *item) {
        if (item == nullptr) {
            return;
        }
        _nativeItem     = item;
        _nativeItemWuid = item->GetId();
        _createdNative  = true;
        _carriedAway    = false;
        _poseDirty      = true;
    }

    void GroundItem::ApplyRemotePose(bool immediate) {
        if (_nativeItem == nullptr && (_materializationFailed || _carriedAway || !TryMaterialize())) {
            return;
        }
        _heldShadowResolved = true;

        auto *pickable = _nativeItem->GetPickableItem();
        auto *entity   = pickable != nullptr ? pickable->GetEntity() : nullptr;
        if (entity == nullptr) {
            return;
        }

        // Only the elected simulator runs rigid-body physics. Observers keep
        // a static pickup collider at the replicated pose. A native
        // attachment already owns its physical state (for example an arrow
        // embedded in a target).
        // Ammunition takes the static profile only rays hit (4: parts keep
        // geom_colltype_ray alone, no collider flags; 0x180A5619C), so the
        // interaction ray still reaches it and nobody walks into it. The game
        // leaves a round stuck in the ground with no body at all
        // (C_PickableItem::EmbedInEntity 0x182A87DF8 requests profile 0).
        // Requested once per item and role: while a transition is pending the
        // native request re-queues and re-registers the item's update
        // (0x181F1BF20), and this runs every frame for a moving stack.
        const auto *missile = Shared::Game::FindMissileClass(item.itemClass);
        const int profile   = IsSimulating() ? 1 : (missile != nullptr && !missile->weapon ? 4 : 2);
        if (entity->GetParent() == nullptr && (_profiledItem != _nativeItem || _requestedProfile != profile)) {
            pickable->RequestPhysicsProfile(profile, {});
            _profiledItem     = _nativeItem;
            _requestedProfile = profile;
        }

        const auto now                 = NowMilliseconds();
        const glm::vec3 targetPosition = resting ? restPosition : position;
        const glm::quat targetRotation = glm::normalize(resting ? restRotation : rotation);
        // A moving stack is smoothed over part of the transform interval; rest,
        // construction and a handover are exact.
        constexpr float kObserverSmoothingMs = 100.0F;
        if (immediate || resting || !_visualPoseKnown) {
            _visualPosition  = targetPosition;
            _visualRotation  = targetRotation;
            _visualPoseKnown = true;
        }
        else {
            const float blend = std::clamp(static_cast<float>(now - _lastVisualUpdate) / kObserverSmoothingMs, 0.0F, 1.0F);
            _visualPosition   = glm::mix(_visualPosition, targetPosition, blend);
            _visualRotation   = glm::normalize(glm::slerp(_visualRotation, targetRotation, blend));
        }
        _lastVisualUpdate = now;
        // Embedded arrows are children of the hit entity. Replicated poses
        // are world-space, so SetWorldTM must resolve the parent transform.
        // Keep physics enabled in the transform update so the pickup collider
        // follows the item too (CPhysicalProxy::OnEntityXForm 0x18080DA20).
        entity->SetWorldTM(SDK::Matrix34FromTransform(KCDC::Common::ToNative(_visualPosition), KCDC::Common::ToNative(glm::normalize(_visualRotation)), SDK::Vec3 {1.0F, 1.0F, 1.0F}));
        _poseDirty = false;
    }

    bool GroundItem::TryMaterialize() {
        if (!KCDC::Features::Items::CustomItemModule::Get().Ready() || Shared::Game::FindInventoryPropertyClass(Shared::Game::CryGuidToString(item.itemClass.low, item.itemClass.high)) == nullptr) {
            return false;
        }
        if (!item.IsSane() || !Shared::Game::IsValidMissileGroundItem(item) || !Shared::Inventory::AreSupportedOnEquipBuffs(item)) {
            return false;
        }
        const auto *definition = Shared::Game::FindInventoryPropertyClass(Shared::Game::CryGuidToString(item.itemClass.low, item.itemClass.high));
        if (definition == nullptr || definition->questItem || (item.amount != 1 && !definition->nativeStackable)) {
            _materializationFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("Ground replica {} cannot create quantity {} as one native item", GetNetworkID(), item.amount);
            return false;
        }

        auto *sourceHuman = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(sourcePlayerNetworkId);
        if (recoveredItemWuid != 0 && sourceHuman != nullptr && sourceHuman->IsOwner()) {
            if (auto *recovered = KCDC::Core::gApplication->GetPlayerInventoryService().TakeRecoveredAmmunition(recoveredItemWuid, item)) {
                RebindNative(recovered);
                if (!ApplyMissileProperties(recovered)) {
                    _materializationFailed = true;
                    CleanupNative();
                    return false;
                }
                Framework::Logging::GetLogger(kLogger)->debug("Ground item {} adopted recovered ammunition {}", GetNetworkID(), recoveredItemWuid);
                return true;
            }
        }

        bool blocked = false;
        if (TryAdoptHeldShadow(blocked)) {
            return true;
        }
        if (blocked) {
            return false;
        }
        bool waiting = false;
        if (TryAdoptCarriedDrop(waiting)) {
            return true;
        }
        if (waiting) {
            return false;
        }

        return CreateWorldItem();
    }

    /**
     * Hands a put-down prop the item its carry dropped.
     *
     * Every machine already has that item: the carrier's own game dropped it
     * from their hands, and every other one played the same put-down on its
     * copy of them. Taking it over keeps the item the physics is already
     * moving, where a second one would appear beside it and the first vanish.
     */
    bool GroundItem::TryAdoptCarriedDrop(bool &waiting) {
        if (!carryable || sourcePlayerNetworkId == 0) {
            return false;
        }
        auto *sourceHuman = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(sourcePlayerNetworkId);
        if (sourceHuman == nullptr) {
            return false;
        }

        const SDK::EntityId carrierEntityId = sourceHuman->ResolveBodyEntityId();
        auto &players                       = KCDC::Core::gApplication->GetPlayerService();
        if (SDK::wh::entitymodule::C_Item *dropped = players.TakeDroppedCarryItem(carrierEntityId, item.itemClass)) {
            _nativeItem     = dropped;
            _nativeItemWuid = dropped->GetId();
            _createdNative  = true;
            Framework::Logging::GetLogger(kLogger)->info("Ground item {} takes over the item {} its carry dropped", GetNetworkID(), _nativeItemWuid);
            return true;
        }

        // The prop can reach a machine before the put-down that makes it has
        // played there; waited for, within reason.
        const std::uint64_t now = NowMilliseconds();
        if (_adoptWaitUntil == 0) {
            _adoptWaitUntil = now + kCarriedDropWaitMs;
        }
        waiting = now < _adoptWaitUntil && players.IsCarryingItem(carrierEntityId, item.itemClass);
        return false;
    }

    /**
     * Hands this replica the shadow the source player was holding.
     *
     * A drop that retires a held item already has a body in the world -- the
     * copy's own -- so it is taken over rather than built again. `blocked`
     * says the shadow is there but not ready to hand over yet, which is a
     * reason to wait rather than to create a second stack.
     */
    bool GroundItem::TryAdoptHeldShadow(bool &blocked) {
        if (_heldShadowResolved) {
            return false;
        }
        auto *sourceHuman = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(sourcePlayerNetworkId);
        if (sourceHuman == nullptr || sourceHuman->IsOwner()) {
            return false;
        }

        if (auto *shadow = sourceHuman->ReleaseHeldShadowForGroundDrop(item.itemClass, retiresHeldItem)) {
            _heldShadowResolved = true;
            _nativeItem     = shadow;
            _nativeItemWuid = shadow->GetId();
            _createdNative  = true;
            if (!ApplyMissileProperties(shadow)) {
                _materializationFailed = true;
                blocked                = true;
                return false;
            }
            return true;
        }

        blocked = retiresHeldItem && sourceHuman->HasHeldShadow(item.itemClass);
        return false;
    }

    bool GroundItem::CreateWorldItem() {
        auto *world = WorldInventory();
        if (world == nullptr) {
            return false;
        }

        if (_materializeAttempts >= kMaxMaterializeAttempts) {
            _materializationFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("Gave up creating the world item for ground replica {} after {} attempts", GetNetworkID(), kMaxMaterializeAttempts);
            return false;
        }
        ++_materializeAttempts;

        // A null result may follow a successful add, so the stack is
        // identified once and an ambiguous creation is never retried.
        const std::vector<SDK::wh::entitymodule::C_Item *> created = CreateWorldItems(item.itemClass, item.amount, item.quality, item.health, item.condition);
        std::uint64_t createdAmount                                = 0;
        for (const SDK::wh::entitymodule::C_Item *native : created) {
            createdAmount += native->GetAmount();
        }
        if (created.size() != 1 || createdAmount != item.amount) {
            // Creation can split or partly succeed. None of these items has
            // a replica binding yet; remove every new item from this attempt.
            for (SDK::wh::entitymodule::C_Item *native : created) {
                world->RemoveItem(native);
            }
            _materializationFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("Ground replica {} requested {} units; creation returned {} items with {} units; discarded the new items", GetNetworkID(), item.amount, created.size(), createdAmount);
            return false;
        }

        _nativeItem = created.front();
        _nativeItemWuid = _nativeItem->GetId();
        _createdNative  = true;
        if (!ApplyMissileProperties(_nativeItem)) {
            _materializationFailed = true;
            CleanupNative();
            return false;
        }
        if (item.phase >= 0) {
            _nativeItem->InvokeSetItemPhaseIdRaw(item.phase);
        }

        _nativeItem->SpawnPickable(&NoopPickableBound, KCDC::Common::ToNative(resting ? restPosition : position), KCDC::Common::ToNative(glm::normalize(resting ? restRotation : rotation)));
        _nativeItemWuid = _nativeItem->GetId();
        _createdNative  = true;
        return true;
    }

    bool GroundItem::ApplyMissileProperties(SDK::wh::entitymodule::C_Item *native) const {
        KCDC::Features::Items::CustomItemModule::Get().Bind(*native, item);
        std::string error;
        if (!KCDC::Features::Items::ApplyNativeItemEffects(*native, item, error)) {
            Framework::Logging::GetLogger(kLogger)->error("World item effect projection failed: {}", error);
            return false;
        }
        if (Shared::Game::FindMissileClass(item.itemClass) == nullptr) {
            return true;
        }

        native->SetQuality(item.quality);
        native->InvokeSetItemHealthRaw(item.health);

        if (auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(native->EnsureRuntimeData())) {
            runtime->SetPoison({item.poisonBuff.low, item.poisonBuff.high}, item.poisonCharges);
            return true;
        }
        return false;
    }

    // --- Teardown ---------------------------------------------------------

    void GroundItem::CleanupNative() {
        if (_nativeItem == nullptr) {
            return;
        }

        // The engine addresses items by WUID; a stale C_Item * is not something
        // it will report. Confirm the stack is still the one this replica made
        // before handing it to the holder teardown.
        if (GetNativeItem() == nullptr) {
            _nativeItem     = nullptr;
            _profiledItem   = nullptr;
            _nativeItemWuid = 0;
            _createdNative  = false;
            return;
        }

        RemoveGroundRepresentation();
        _nativeItem     = nullptr;
        _profiledItem   = nullptr;
        _nativeItemWuid = 0;
        _createdNative  = false;
    }

    /**
     * Takes the stack back out of the world.
     *
     * Standalone items are RPG-owned by world inventory. Held-item drops can
     * retain copy inventory as RPG owner while world inventory is only the
     * physical holder, so whichever exact relationship is present is the one
     * torn down.
     */
    void GroundItem::RemoveGroundRepresentation() {
        auto *world = WorldInventory();
        if (world == nullptr) {
            return;
        }

        if (_nativeItem->GetOwner() == world) {
            world->RemoveItem(_nativeItem);
            return;
        }

        if (_nativeItem->GetHolder() == world) {
            auto *owner = _nativeItem->GetOwner();
            if (_nativeItem->GetPickableItem() != nullptr) {
                _nativeItem->UnbindPickable();
            }
            if (owner != nullptr) {
                owner->TransferItemToHolder(_nativeItem, true);
            }
            return;
        }

        if (_nativeItem->GetPickableItem() != nullptr) {
            _nativeItem->UnbindPickable();
            Framework::Logging::GetLogger(kLogger)->warn("Unbound ground representation WUID {} whose physical holder was no longer world inventory", _nativeItemWuid);
        }
    }
} // namespace KCDC::Features::GroundItem
