#include "door_service.h"

#include "door_scripting.h"

#include "features/player/player.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_service.h"
#include "common/module_support.h"
#include "core/presentation_module.h"

#include "core/application.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/cryengine/scriptsystem/script_arguments_function.h"
#include "sdk/wh/entitymodule/c_anim_door.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger                    = "Door";
        constexpr std::size_t kMaxInitializedDoorsQueued = 4096;
        constexpr std::size_t kMaxNativeEdgesQueued      = 4096;

        /** The world the server last said this player is in; the global one until it has. */
        std::uint32_t ViewerWorld() {
            return KCDC::Core::gApplication->GetPlayerInventoryService().SyncedWorld().value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        }
        // How long a transition that cannot be reported is kept. Longer than
        // any door animation, so the native manipulation ending is what
        // normally retires one and this is only the backstop.
        constexpr std::uint64_t kPendingTransitionDeadlineMs = 5000;

        constexpr float kDoorAnimateOperation = 0.0F;

        constexpr char kDoorLuaDispatcher[]   = R"lua(
local door = AnimDoor
local animate = DoPlayAnimation or (door and door.DoPlayAnimation)
animate = assert(animate, "door.DoPlayAnimation is not loaded")
local findLinks = assert(XGenAIModule.FindLinks, "XGenAIModule.FindLinks is not loaded")
local nextValue = next
local protectedCall = pcall
local raise = error
local rawValue = rawget

local function getSuspiciousVolume(self)
    local links = findLinks(self.this.id, "crime_suspiciousDoor_reverse")
    if links == nil or nextValue(links) == nil then
        return nil
    end
    return links[1]
end

return function(operation, self, ...)
    if operation == 0 then
        local direction, forceTime, useSound, customAnim, usePlayerAnim, userId = ...
        local animation = self:BuildObjectAnimationName(userId, customAnim)
        if animation == nil or animation == "" then
            raise("door has no object animation for this actor")
        end
        -- Retail calls next(FindLinks(...)) without checking whether
        -- FindLinks returned nil. Substitute the corrected implementation
        -- only for this authoritative call, then restore the entity exactly.
        local previous = rawValue(self, "GetSuspiciousVolume")
        self.GetSuspiciousVolume = getSuspiciousVolume
        local succeeded, result = protectedCall(animate, self, direction, forceTime, useSound, animation, usePlayerAnim, userId)
        self.GetSuspiciousVolume = previous
        if not succeeded then
            raise(result)
        end
        return result
    end
    error("invalid door synchronization operation")
end
)lua";

        struct NativeDoorState {
            SDK::IEntity *entity            = nullptr;
            SDK::IScriptTable *entityScript = nullptr;
            float direction                 = 0.0F;
            float inUse                     = 0.0F;
            float needUpdate                = 0.0F;
            float updateAfterLoad           = 0.0F;
            float targetTime                = 0.0F;
            bool locked                     = false;
            std::string lastAnimation;
            std::string currentAnimation;
            SDK::ScriptAnyValue lastUsedBy;
            bool hasLastUsedBy = false;
        };

        std::string BoundedText(const char *text, std::size_t limit) {
            if (text == nullptr) {
                return {};
            }

            std::size_t length = 0;
            while (length <= limit && text[length] != '\0') {
                ++length;
            }
            if (length > limit) {
                return {};
            }

            std::string result(text, length);
            return Shared::RPC::DoorDetail::IsBoundedText(result, limit) ? result : std::string {};
        }

        const char *DoorEventName(Shared::RPC::DoorEvent event) {
            switch (event) {
            case Shared::RPC::DoorEvent::Manipulation: return "manipulation";
            case Shared::RPC::DoorEvent::Lockpicked: return "lockpicked";
            }
            return "invalid";
        }

        /** Answers no and, where the caller asked for one, says why. */
        bool Refuse(const char **failureReason, const char *reason) {
            if (failureReason != nullptr) {
                *failureReason = reason;
            }
            return false;
        }

        bool ResolveDoor(SDK::EntityGUID guid, NativeDoorState &door, const char **failureReason = nullptr, bool animationDetails = true) {
            auto *entitySystem = SDK::IEntitySystem::GetInstance();
            if (entitySystem == nullptr) {
                return Refuse(failureReason, "entity system unavailable");
            }

            auto *entity = entitySystem->GetEntityByGuid(guid);
            if (entity == nullptr) {
                return Refuse(failureReason, "GUID is not loaded");
            }

            door.entity       = entity;
            auto *entityClass = entity->GetClass();
            if (entityClass == nullptr || entityClass->GetName() == nullptr || std::strcmp(entityClass->GetName(), SDK::EntityClassNames::AnimDoor) != 0) {
                return Refuse(failureReason, "GUID does not resolve to AnimDoor");
            }
            auto *proxy        = entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script));
            auto *entityScript = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
            if (entityScript == nullptr) {
                return Refuse(failureReason, "AnimDoor entity script table unavailable");
            }
            if (!entityScript->GetValue("nDirection", door.direction)) {
                return Refuse(failureReason, "AnimDoor nDirection unavailable");
            }
            if (!entityScript->GetValue("inUse", door.inUse)) {
                return Refuse(failureReason, "AnimDoor inUse unavailable");
            }
            if (!entityScript->GetValue("bLocked", door.locked)) {
                return Refuse(failureReason, "AnimDoor bLocked unavailable");
            }

            door.entityScript = entityScript;
            if (!animationDetails) {
                return true;
            }
            const char *lastAnimation = nullptr;
            if (entityScript->GetValue("lastAnim", lastAnimation)) {
                door.lastAnimation = BoundedText(lastAnimation, Shared::RPC::kMaxDoorObjectAnimationLength);
            }
            const char *currentAnimation = nullptr;
            if (entityScript->GetValue("curAnim", currentAnimation)) {
                door.currentAnimation = BoundedText(currentAnimation, Shared::RPC::kMaxDoorObjectAnimationLength);
            }
            entityScript->GetValue("bNeedUpdate", door.needUpdate);
            entityScript->GetValue("nUpdateAfterLoad", door.updateAfterLoad);
            entityScript->GetValue("fTargetTime", door.targetTime);
            door.lastUsedBy.type = SDK::EScriptAnyType::Any;
            door.hasLastUsedBy   = entityScript->GetValueAny("lastUsedBy", door.lastUsedBy);
            return true;
        }

        bool CaptureDoorDispatcher(SDK::IScriptTable *entityScript, SDK::HScriptFunction &dispatcher, std::string &errorMessage, bool globalEnvironment) {
            auto *scriptSystem = entityScript != nullptr ? entityScript->GetScriptSystem() : nullptr;
            if (scriptSystem == nullptr) {
                errorMessage = "door script system is unavailable";
                return false;
            }
            SDK::IScriptTable *environment = globalEnvironment ? nullptr : entityScript;
            const char *description        = globalEnvironment ? "KCDC early global AnimDoor dispatcher" : "KCDC entity-environment AnimDoor dispatcher";
            if (!scriptSystem->ExecuteBuffer(kDoorLuaDispatcher, sizeof(kDoorLuaDispatcher) - 1, description, environment)) {
                errorMessage = std::string("could not execute the ") + (globalEnvironment ? "early global" : "entity-environment") + " AnimDoor dispatcher chunk";
                return false;
            }

            SDK::ScriptAnyValue function;
            function.type = SDK::EScriptAnyType::Function;
            if (!scriptSystem->PopStackValue(function)) {
                errorMessage = "AnimDoor dispatcher chunk did not return a function";
                return false;
            }

            dispatcher = function.function;
            if (dispatcher == 0) {
                errorMessage = "AnimDoor dispatcher returned an invalid function reference";
                return false;
            }
            return true;
        }

        bool BeginDoorDispatcherCall(SDK::IScriptTable *selfTable, SDK::HScriptFunction dispatcher, float operation, const char *operationName, std::string &errorMessage) {
            auto *scriptSystem = selfTable != nullptr ? selfTable->GetScriptSystem() : nullptr;
            if (scriptSystem == nullptr || !scriptSystem->BeginCallFunction(dispatcher)) {
                errorMessage = std::string("retained AnimDoor dispatcher could not begin operation ") + operationName;
                return false;
            }
            scriptSystem->PushFuncParamAny(SDK::ScriptAnyValue(operation));
            scriptSystem->PushFuncParamAny(SDK::ScriptAnyValue(selfTable));
            return true;
        }

        bool EndDoorCall(SDK::IScriptTable *script, std::string &errorMessage) {
            auto *scriptSystem = script->GetScriptSystem();
            if (scriptSystem->EndCall()) {
                return true;
            }

            SDK::ScriptAnyValue error;
            if (!scriptSystem->PopStackValue(error)) {
                errorMessage = "Lua pcall failed without a convertible error value";
            }
            else if (error.type == SDK::EScriptAnyType::String && error.str != nullptr) {
                errorMessage = error.str;
            }
            else {
                errorMessage = "Lua pcall failed with error value type " + std::to_string(static_cast<std::int32_t>(error.type));
            }
            return false;
        }

        /** Calls AnimDoor.lua's exact six-argument DoPlayAnimation surface. */
        bool AnimateDoor(SDK::IScriptTable *entityScript, SDK::HScriptFunction dispatcher, bool open, bool snapshot, SDK::EntityId userEntityId, const std::string &objectAnimation, std::string &errorMessage) {
            if (!BeginDoorDispatcherCall(entityScript, dispatcher, kDoorAnimateOperation, "DoPlayAnimation", errorMessage)) {
                return false;
            }

            auto *scriptSystem = entityScript->GetScriptSystem();
            scriptSystem->PushFuncParamAny(SDK::ScriptAnyValue(open ? 1.0F : -1.0F));
            scriptSystem->PushFuncParamAny(snapshot ? SDK::ScriptAnyValue(1.0F) : SDK::ScriptAnyValue::Nil());
            scriptSystem->PushFuncParamAny(snapshot ? SDK::ScriptAnyValue(false) : SDK::ScriptAnyValue::Nil());
            scriptSystem->PushFuncParamAny(objectAnimation.empty() ? SDK::ScriptAnyValue::Nil() : SDK::ScriptAnyValue(objectAnimation.c_str()));
            // The exact actor action is replayed separately, so no body is
            // ever driven from here. The user still has to be named when no
            // clip is: it is what GetObjectAnimName resolves an actor from,
            // and it names nothing without one.
            scriptSystem->PushFuncParamAny(SDK::ScriptAnyValue(false));
            scriptSystem->PushFuncParamAny(userEntityId == 0 ? SDK::ScriptAnyValue::Nil() : SDK::ScriptAnyValue::Handle(userEntityId));
            return EndDoorCall(entityScript, errorMessage);
        }

        /**
         * Locks or unlocks a door through `C_LockBase::SetLocked`, the engine's
         * own entry. A lock pushes `dontClose = true` as AnimDoor.lua's
         * `Lock(dontClose)` takes it, so the replicated open bit stays the
         * server's; an unlock takes no argument.
         */
        bool SetDoorLocked(SDK::IEntity *entity, bool locked, std::string &errorMessage) {
            SDK::wh::entitymodule::C_AnimDoor *door = SDK::wh::entitymodule::FindAnimDoorExtension(entity);
            if (door == nullptr) {
                errorMessage = "the entity has no AnimDoor extension";
                return false;
            }
            if (locked) {
                SDK::ScriptBooleanArgument dontClose(true);
                SDK::ScriptArgumentsFunction arguments(dontClose);
                door->SetLocked(true, arguments);
            }
            else {
                door->SetLocked(false);
            }
            return true;
        }
    } // namespace

    bool DoorService::EnsureDoorDispatcher(SDK::IScriptTable *entityScript, std::string &errorMessage, bool preferGlobalEnvironment) {
        auto *scriptSystem          = entityScript != nullptr ? entityScript->GetScriptSystem() : nullptr;
        const std::uint64_t context = _nativeContext.load();
        if (scriptSystem == nullptr) {
            errorMessage = "door script system is unavailable";
            return false;
        }
        if (_dispatcherFunction != 0 && _dispatcherScriptSystem == scriptSystem && _dispatcherContext == context) {
            return true;
        }

        if (_dispatcherFunction != 0) {
            _dispatcherScriptSystem = nullptr;
            _dispatcherFunction     = 0;
            _dispatcherContext      = 0;
        }

        SDK::HScriptFunction dispatcher = 0;
        if (!CaptureDoorDispatcher(entityScript, dispatcher, errorMessage, preferGlobalEnvironment)) {
            if (!preferGlobalEnvironment) {
                return false;
            }
            const std::string globalError = errorMessage;
            if (!CaptureDoorDispatcher(entityScript, dispatcher, errorMessage, false)) {
                errorMessage = globalError + "; entity-environment fallback also failed: " + errorMessage;
                return false;
            }
        }

        _dispatcherScriptSystem = scriptSystem;
        _dispatcherFunction     = dispatcher;
        _dispatcherContext      = context;
        return true;
    }

    void DoorService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Cannot register door RPCs: network peer is unavailable during client initialization");
            return;
        }

        net->RegisterRPC<Shared::RPC::DoorNearestQuery>([](const Shared::RPC::DoorNearestQuery &query, MafiaNet::Packet *) {
            if (query.IsSane()) {
                KCDC::Core::gApplication->GetDoorService().QueueNearestQuery(query);
            }
        });
    }

    Door *DoorService::Find(SDK::EntityGUID doorGuid) const {
        const auto found = _replicaIndex.find(doorGuid);
        return found != _replicaIndex.end() ? found->second : nullptr;
    }

    void DoorService::QueueDespawn(SDK::EntityId entityId) {
        std::scoped_lock lock(_despawnMutex);
        _pendingDespawns.push_back(entityId);
    }

    void DoorService::RefreshReplicaIndex() {
        // An area streaming in asks about every door it builds; walking the
        // replica set for each would be quadratic.
        _replicaIndex.clear();
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        // Each door is posed from the copy of the player's own world, else the
        // global one; another world's copy, which a player in the global world
        // also holds, never. A copy left out is kept unbound, so it tells the
        // door its state again once it is the one shown.
        const std::uint32_t world = ViewerWorld();
        replication->ForEach<Door>([this, world](Door *door) {
            const int rank = Shared::Game::WorldCopyRank(door->copyWorld, world);
            if (door->doorGuid == 0 || rank == 0) {
                // A hung door this player is not shown does not stand for them.
                door->DestroyNative();
                door->Unbind();
                return;
            }
            const auto [entry, added] = _replicaIndex.emplace(door->doorGuid, door);
            if (!added) {
                Door *shadowed = door;
                if (rank > Shared::Game::WorldCopyRank(entry->second->copyWorld, world)) {
                    shadowed      = entry->second;
                    entry->second = door;
                }
                shadowed->Unbind();
            }
        });
    }

    void DoorService::QueueNearestQuery(const Shared::RPC::DoorNearestQuery &query) {
        std::lock_guard lock(_incomingMutex);
        if (_acceptIncoming.load()) {
            _incomingNearestQueries.push_back(query);
        }
    }

    void DoorService::NativeContextChanged() {
        _captureLocalEvents.store(false);
        ++_nativeContext;
    }

    void DoorService::NotifyDoorInitialized(SDK::wh::entitymodule::C_AnimDoor *door) {
        SDK::IEntity *entity            = door != nullptr ? door->GetEntity() : nullptr;
        SDK::IEntityProxy *proxy        = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        SDK::IScriptTable *entityScript = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        if (entityScript == nullptr) {
            return;
        }
        entityScript->AddRef();
        SDK::ScriptTablePtr entityScriptReference(entityScript);

        std::string errorMessage;
        if (!EnsureDoorDispatcher(entityScript, errorMessage, true)) {
            Framework::Logging::GetLogger(kLogger)->error("Could not capture AnimDoor methods at native Init: entity={}, guid={:#x}, context={}, reason={}", entity->GetId(), entity->GetGuid(), _nativeContext.load(), errorMessage);
        }

        // Not necessarily the game thread, so only record it.
        std::scoped_lock initializedLock(_initializedDoorsMutex);
        if (_initializedDoors.size() < kMaxInitializedDoorsQueued) {
            _initializedDoors.push_back(entity->GetGuid());
        }
    }

    void DoorService::ObserveNativeContext(const std::string &level) {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context && _observedLevel == level) {
            return;
        }

        if (_dispatcherFunction != 0 && _dispatcherContext != context) {
            // The former Lua VM may already be gone, and the registry
            // reference died with it. Do not enter it just to release one.
            _dispatcherScriptSystem = nullptr;
            _dispatcherFunction     = 0;
            _dispatcherContext      = 0;
        }
        _observedContext   = context;
        _observedLevel     = level;
        _observedLevelHash = level.empty() ? 0 : Shared::RPC::DoorLevelHash(level);
        {
            std::scoped_lock initializedLock(_initializedDoorsMutex);
            _initializedDoors.clear();
        }
        for (const auto &transition : _pendingLocalTransitions) {
            KCDC::Core::PresentationModule::Get().CancelLocalDoor(transition.second.presentationSequence);
        }
        _pendingLocalTransitions.clear();
        _accessSide = {};
        {
            std::scoped_lock nativeEdgesLock(_nativeEdgesMutex);
            _nativeEdges.clear();
        }
        _strayedDoors.clear();

        {
            // The entity ids named the world that went.
            std::scoped_lock despawnLock(_despawnMutex);
            _pendingDespawns.clear();
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication != nullptr) {
            replication->ForEach<Door>([](Door *door) {
                door->ForgetNative();
                door->Unbind();
            });
        }
        _animationDoorEntityId = 0;
        _animationUserEntityId = 0;
        _applyDepth            = 0;
        // The handlers were references into the Lua state that went.
        _promptTargetEntityId = 0;
        _promptHandlers.clear();
    }

    void DoorService::DrainInitializedDoors() {
        std::vector<SDK::EntityGUID> initialized;
        {
            std::scoped_lock lock(_initializedDoorsMutex);
            initialized.swap(_initializedDoors);
        }

        // The level built these, so any replica of one is bound to an entity
        // that no longer exists; ApplyReplicatedState tells the new one what
        // the server says on the next pass.
        for (const SDK::EntityGUID doorGuid : initialized) {
            if (Door *known = Find(doorGuid)) {
                known->Unbind();
            }
        }
    }

    void DoorService::DrainNativeEdges() {
        std::vector<SDK::EntityId> edges;
        {
            std::scoped_lock lock(_nativeEdgesMutex);
            edges.swap(_nativeEdges);
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        std::vector<SDK::EntityId> deferred;
        for (const SDK::EntityId entityId : edges) {
            // A use still being reported reads the door when it is sent, so
            // the world's edge on the same door waits for that to be done.
            if (_pendingLocalTransitions.contains(entityId)) {
                deferred.push_back(entityId);
                continue;
            }
            // Locks raise these too, containers' among them; only a door has
            // a replica under its GUID.
            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(entityId) : nullptr;
            Door *door           = entity != nullptr ? Find(entity->GetGuid()) : nullptr;
            if (door != nullptr) {
                door->MarkStrayed();
            }
        }
        if (!deferred.empty()) {
            std::scoped_lock lock(_nativeEdgesMutex);
            _nativeEdges.insert(_nativeEdges.end(), deferred.begin(), deferred.end());
        }

        for (const SDK::EntityGUID doorGuid : _strayedDoors) {
            if (Door *door = Find(doorGuid)) {
                door->MarkStrayed();
            }
        }
        _strayedDoors.clear();
    }

    void DoorService::ApplyReplicatedState() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || _observedLevelHash == 0) {
            return;
        }

        // The index was built this tick, so walking it costs nothing extra;
        // ForEach walks every replicated entity with a dynamic_cast. A settled
        // door is never read: nothing moves one without an edge unsettling it.
        auto *entitySystem      = SDK::IEntitySystem::GetInstance();
        const std::uint64_t now = MafiaNet::GetTime();
        for (const auto &entry : _replicaIndex) {
            Door *door = entry.second;
            if (door->IsSettled() || door->levelHash != _observedLevelHash || !door->ReadyToRetry(now)) {
                continue;
            }

            SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntityByGuid(door->doorGuid) : nullptr;
            if (entity == nullptr) {
                // Streamed out; told again when the level builds it back.
                door->Unbind();
                continue;
            }
            if (!door->NeedsApply(entity)) {
                continue;
            }
            // A body's use arrives twice: the door's new state, and the
            // interaction that swings it, held until the body is drawn at the
            // push. Applied first, the state would land as a pose and leave
            // the swing nothing to do, so it waits for the interaction.
            if (KCDC::Core::PresentationModule::Get().HasPendingDoorInteraction(door->doorGuid, door->copyWorld)) {
                continue;
            }
            if (ApplyDurable(*door)) {
                door->MarkApplied(entity);
                door->NoteApplySucceeded();
            }
            else {
                door->NoteApplyFailed(now);
            }
        }
    }

    Shared::Game::DoorPassage DoorService::PreparePreviewPassage(std::uint64_t guid, SDK::EntityId actorId) {
        NativeDoorState native;
        if (!ResolveDoor(guid, native, nullptr, false)) {
            return native.entity == nullptr ? Shared::Game::DoorPassage::Ready : Shared::Game::DoorPassage::Blocked;
        }
        if (native.direction > 0.0F) {
            return native.inUse != 0.0F ? Shared::Game::DoorPassage::Opening : Shared::Game::DoorPassage::Ready;
        }
        if (native.inUse != 0.0F) {
            return Shared::Game::DoorPassage::Opening;
        }
        const auto *actor = SDK::IEntitySystem::GetInstance()->GetEntity(actorId);
        if (!actor) {
            return Shared::Game::DoorPassage::Blocked;
        }
        const auto doorPosition  = native.entity->GetWorldPos();
        const auto actorPosition = actor->GetWorldPos();
        const auto forward       = native.entity->GetForwardDir();
        const bool front         = (doorPosition.x - actorPosition.x) * forward.x + (doorPosition.y - actorPosition.y) * forward.y >= 0.0F;
        // The NPC actor class has no entry in some doors' object-clip tables.
        // Use the same selector as replicated swings; the hook still supplies
        // the NPC's approach side, and no player-body animation is requested.
        return ApplyNativeState(guid, true, false, front, KCDC::Common::LocalActorEntityId(), nullptr) ? Shared::Game::DoorPassage::Opening : Shared::Game::DoorPassage::Blocked;
    }

    bool DoorService::ApplyDurable(Door &door) {
        return ApplyNativeState(door.doorGuid, door.open, door.locked, door.openedFromFront, KCDC::Common::LocalActorEntityId(), &door);
    }

    bool DoorService::ApplyNativeState(std::uint64_t guid, bool open, bool locked, bool fromFront, SDK::EntityId userId, const Door *replica) {
        NativeDoorState native;
        const char *failure = nullptr;
        if (!ResolveDoor(guid, native, &failure)) {
            return false;
        }
        native.entityScript->AddRef();
        SDK::ScriptTablePtr entityScriptReference(native.entityScript);

        const bool openDiffers   = (native.direction > 0.0F) != open;
        const bool lockedDiffers = native.locked != locked;
        if (!openDiffers && !lockedDiffers) {
            return true;
        }
        // A door still swinging is taken as it is: DoPlayAnimation reverses a
        // running clip from where it stands, so the server's word does not
        // wait for the swing it contradicts to finish.

        std::string dispatcherError;
        if (!EnsureDoorDispatcher(native.entityScript, dispatcherError)) {
            Framework::Logging::GetLogger(kLogger)->warn("Door dispatcher unavailable for {:#x}: {}", guid, dispatcherError);
            return false;
        }

        // A change the server made itself, or a door put back after it moved
        // on its own, reaches a door this client already has built as a
        // swing, the way AnimDoor:Open plays one; so does a door the level
        // has just built, whose state nobody here has seen yet. A pose on
        // one -- the clip set to its last frame, as AnimDoor:OnLoad restores
        // a saved door -- left the leaf drawn shut while the script held it
        // open, and a use then swung it shut from open. A body's own change
        // on a door already told its state was replayed as an interaction,
        // so only that is a pose to arrive at.
        const bool live = !replica || !replica->IsBoundTo(native.entity) || replica->changedByServer || replica->IsStrayed();

        // The clip is the game's own choice, made through the selector from
        // the side the server recorded. It names nothing without a user it
        // can resolve to an actor, so the local body stands in, as the
        // player does for AnimDoor:Open; with none yet, the last clip this
        // door played is the only name there is.
        const SDK::EntityId selectorUserEntityId = userId;
        const std::string clip                   = selectorUserEntityId != 0 ? std::string {} : native.lastAnimation;

        ++_applyDepth;
        _animationDoorEntityId      = native.entity->GetId();
        _animationUserEntityId      = selectorUserEntityId;
        _animationOpening           = open;
        _animationAccessedFromFront = fromFront;

        bool applied = true;
        if (openDiffers) {
            if (!live) {
                native.entityScript->SetValue("nUpdateAfterLoad", 2.0F);
            }
            std::string luaError;
            applied = AnimateDoor(native.entityScript, _dispatcherFunction, open, !live, selectorUserEntityId, clip, luaError);
            if (!applied) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not {} door {:#x} to open={}: {}", live ? "swing" : "restore", guid, open, luaError);
            }
            else if (!live) {
                native.entityScript->SetValue("curAnim", "");
                native.entityScript->SetValue("inUse", 0.0F);
            }
        }
        if (applied && lockedDiffers) {
            std::string luaError;
            applied = SetDoorLocked(native.entity, locked, luaError);
            if (!applied) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not set door {:#x} locked={}: {}", guid, locked, luaError);
            }
        }

        _animationDoorEntityId = 0;
        _animationUserEntityId = 0;
        --_applyDepth;
        return applied;
    }

    void DoorService::ApplyRemoteInteraction(const Shared::RPC::DoorInteraction &interaction) {
        if (interaction.levelHash != _observedLevelHash) {
            return;
        }
        // Another world's door: its body is streamed here, its door copy is
        // not the one this player is shown, and the native door would be
        // left where that copy put it, settled and never corrected.
        const Door *shown = Find(interaction.doorGuid);
        if (shown != nullptr ? shown->copyWorld != interaction.copyWorld : Shared::Game::WorldCopyRank(interaction.copyWorld, ViewerWorld()) == 0) {
            return;
        }

        NativeDoorState native;
        const char *failure = nullptr;
        if (!ResolveDoor(interaction.doorGuid, native, &failure)) {
            return;
        }
        native.entityScript->AddRef();
        SDK::ScriptTablePtr entityScriptReference(native.entityScript);

        KCDC::Features::Player::Human *source = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(interaction.actorNetworkId);
        SDK::wh::entitymodule::C_Actor *actor = source != nullptr && !source->IsOwner() ? source->ResolveActor() : nullptr;
        const SDK::EntityId doorEntityId      = native.entity->GetId();

        std::string dispatcherError;
        if (!EnsureDoorDispatcher(native.entityScript, dispatcherError)) {
            Framework::Logging::GetLogger(kLogger)->warn("Could not capture the AnimDoor dispatcher for {:#x}: {}", interaction.doorGuid, dispatcherError);
            return;
        }

        if (interaction.event == Shared::RPC::DoorEvent::Lockpicked) {
            ReplayLockpicked(native.entity);
            return;
        }

        ++_applyDepth;
        _animationDoorEntityId      = doorEntityId;
        _animationUserEntityId      = actor != nullptr ? actor->GetEntityId() : 0;
        _animationOpening           = interaction.open;
        _animationAccessedFromFront = interaction.fromFront;

        if (interaction.event == Shared::RPC::DoorEvent::Manipulation) {
            // The human half first, as retail orders it. This dispatches
            // state event 103 and the engine builds and drives the action
            // from there; the actor model's interaction target says whether
            // it is still running, so nothing here tracks it.
            const char *actionName = Shared::RPC::DoorActorActionName(interaction.actorAction);
            if (actor != nullptr && actionName != nullptr) {
                actor->StartInteractiveActionByName(actionName, doorEntityId, true, 1.0F);
            }

            // A door that already stands there -- the push changed nothing on
            // the server, or this viewer had it there already -- would start
            // the clip over from the other end.
            std::string luaError;
            if ((native.direction > 0.0F) != interaction.open && !AnimateDoor(native.entityScript, _dispatcherFunction, interaction.open, false, _animationUserEntityId, interaction.objectAnimation, luaError)) {
                Framework::Logging::GetLogger(kLogger)->warn("Door {:#x} refused the replicated transition '{}': {}", interaction.doorGuid, interaction.objectAnimation, luaError);
            }
        }

        if (native.locked != interaction.locked) {
            std::string luaError;
            if (!SetDoorLocked(native.entity, interaction.locked, luaError)) {
                Framework::Logging::GetLogger(kLogger)->warn("Door {:#x} refused lock={} from an interaction: {}", interaction.doorGuid, interaction.locked, luaError);
            }
        }

        _animationDoorEntityId = 0;
        _animationUserEntityId = 0;
        --_applyDepth;
    }

    bool DoorService::ReplayLockpicked(SDK::IEntity *entity) {
        SDK::wh::entitymodule::C_AnimDoor *door = SDK::wh::entitymodule::FindAnimDoorExtension(entity);
        if (door == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Replicated lockpick found no AnimDoor extension");
            return false;
        }

        // What a successful pick does once the lock is open, in its order:
        // the lock base's lockpicked signal, then the entity's OnLockpicked.
        ++_applyDepth;
        door->GetOnLockpickedSignal()->Broadcast();
        door->CallOnLockpickedScript();
        --_applyDepth;
        return true;
    }

    void DoorService::AnswerNearestQueries() {
        std::vector<Shared::RPC::DoorNearestQuery> queries;
        {
            std::lock_guard lock(_incomingMutex);
            queries.swap(_incomingNearestQueries);
        }
        auto *net = queries.empty() ? nullptr : Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *player       = entitySystem != nullptr ? entitySystem->GetEntity(KCDC::Common::LocalActorEntityId()) : nullptr;
        SDK::IEntityItPtr iterator(entitySystem != nullptr ? entitySystem->GetEntityIterator() : nullptr);

        Shared::RPC::DoorNearestAnswer answer;
        if (player != nullptr && iterator) {
            const glm::vec3 playerPosition = KCDC::Common::ToGlm(player->GetWorldPos());
            float nearestDistanceSquared   = std::numeric_limits<float>::max();
            SDK::IEntity *nearestDoor      = nullptr;
            iterator->MoveFirst();
            while (!iterator->IsEnd()) {
                SDK::IEntity *entity           = iterator->Next();
                SDK::IEntityClass *entityClass = entity != nullptr ? entity->GetClass() : nullptr;
                const char *className          = entityClass != nullptr ? entityClass->GetName() : nullptr;
                // A door a level edit took out of the world is not in sight.
                if (className == nullptr || std::strcmp(className, SDK::EntityClassNames::AnimDoor) != 0 || entity->GetGuid() == 0 || entity->IsHidden()) {
                    continue;
                }
                const glm::vec3 separation           = KCDC::Common::ToGlm(entity->GetWorldPos()) - playerPosition;
                const float candidateDistanceSquared = glm::dot(separation, separation);
                if (candidateDistanceSquared < nearestDistanceSquared) {
                    nearestDistanceSquared = candidateDistanceSquared;
                    nearestDoor            = entity;
                }
            }

            NativeDoorState door;
            if (nearestDoor != nullptr && ResolveDoor(nearestDoor->GetGuid(), door)) {
                const char *doorName = nearestDoor->GetName();
                answer.name          = BoundedText(doorName, Shared::RPC::kMaxDoorNameLength);
                answer.doorGuid      = nearestDoor->GetGuid();
                answer.levelHash     = _observedLevelHash;
                answer.distance      = std::sqrt(nearestDistanceSquared);
                answer.open          = door.direction > 0.0F;
                answer.locked        = door.locked;
                answer.found         = answer.levelHash != 0;
            }
        }

        for (const Shared::RPC::DoorNearestQuery &query : queries) {
            answer.requestId = query.requestId;
            net->BroadcastRPC(answer, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void DoorService::Update() {
        _acceptIncoming.store(true);

        const std::string level      = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetCurrentLevelName() : std::string {};
        auto *gameFramework          = SDK::IGameFramework::GetInstance();
        auto *levelSystem            = gameFramework != nullptr ? gameFramework->GetILevelSystem() : nullptr;
        const bool engineLevelLoaded = levelSystem != nullptr && levelSystem->IsLevelLoaded();
        const bool levelReady        = !level.empty() && engineLevelLoaded;

        ObserveNativeContext(level);
        {
            std::vector<SDK::EntityId> despawns;
            {
                std::scoped_lock lock(_despawnMutex);
                despawns.swap(_pendingDespawns);
            }
            if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
                for (const SDK::EntityId entityId : despawns) {
                    entitySystem->RemoveEntity(entityId, false);
                }
            }
        }
        RefreshReplicaIndex();
        if (levelReady) {
            // A hung door stands only once the level it hangs in does.
            for (const auto &entry : _replicaIndex) {
                entry.second->BuildNative();
            }
        }
        DrainInitializedDoors();
        SendPendingTransitions();
        DrainNativeEdges();
        ApplyReplicatedState();
        AnswerNearestQueries();

        // No snapshot to wait for: every door the server holds arrives as a
        // replica, and one that has not is still as the level built it.
        _captureLocalEvents.store(levelReady);
    }

    // --- Local edges ---------------------------------------------------------

    void DoorService::NotifyNativeEdge(SDK::EntityId entityId) {
        // This client's own applies and replays raise these as well.
        if (!_captureLocalEvents.load() || _applyDepth != 0) {
            return;
        }
        std::scoped_lock lock(_nativeEdgesMutex);
        if (_nativeEdges.size() < kMaxNativeEdgesQueued) {
            _nativeEdges.push_back(entityId);
        }
    }

    void DoorService::ClaimNativeEdges(SDK::EntityId doorEntityId) {
        std::scoped_lock lock(_nativeEdgesMutex);
        std::erase(_nativeEdges, doorEntityId);
    }

    void DoorService::NotifyAccessSide(SDK::EntityId doorEntityId, bool accessedFromFront) {
        if (!_captureLocalEvents.load() || _applyDepth != 0) {
            return;
        }
        _accessSide.doorEntityId = doorEntityId;
        _accessSide.fromFront    = accessedFromFront;
    }

    void DoorService::NotifyActorAction(SDK::wh::entitymodule::C_Actor *actor, const char *actionName, SDK::EntityId objectEntityId) {
        if (!_captureLocalEvents.load() || _applyDepth != 0 || actor == nullptr || objectEntityId == 0 || actor->GetEntityId() != KCDC::Common::LocalActorEntityId()) {
            return;
        }

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(objectEntityId) : nullptr;
        NativeDoorState door;
        if (entity == nullptr || entity->GetGuid() == 0 || !ResolveDoor(entity->GetGuid(), door)) {
            return;
        }

        // A use chose the door's clip a moment ago, in the same Lua call, and
        // set it moving. Anything else the player starts on a door is not a
        // transition of it.
        const AccessSide side = std::exchange(_accessSide, AccessSide {});
        if (side.doorEntityId != objectEntityId || door.inUse == 0.0F) {
            return;
        }
        const Shared::RPC::DoorActorAction actorAction = Shared::RPC::DoorActorActionFromName(actionName);
        if (actorAction == Shared::RPC::DoorActorAction::None) {
            Framework::Logging::GetLogger(kLogger)->warn("Local door action '{}' on entity {} is not one this build can replay; the door goes back to the server's state", actionName != nullptr ? actionName : "<null>", objectEntityId);
            _strayedDoors.push_back(entity->GetGuid());
            return;
        }

        // The use is this client's to report. It raised the door's edges on
        // its way here -- a key turned, then the push -- and the report
        // carries both.
        ClaimNativeEdges(objectEntityId);

        if (_pendingLocalTransitions.contains(objectEntityId)) {
            SendPendingTransition(objectEntityId);
            if (_pendingLocalTransitions.contains(objectEntityId)) {
                DiscardPendingTransition(objectEntityId, "a newer use of the door superseded it");
            }
        }

        PendingLocalTransition pending;
        pending.doorGuid             = entity->GetGuid();
        pending.startedAt            = MafiaNet::GetTime();
        pending.actorAction          = actorAction;
        pending.fromFront            = side.fromFront;
        pending.presentationSequence = KCDC::Core::PresentationModule::Get().ReserveLocalDoor();
        if (pending.presentationSequence == 0) {
            _strayedDoors.push_back(pending.doorGuid);
            return;
        }
        CaptureLocalActorFence(pending.actorFence);
        _pendingLocalTransitions.insert_or_assign(objectEntityId, std::move(pending));
    }

    void DoorService::NotifyLockpicked(SDK::EntityId doorEntityId) {
        if (!_captureLocalEvents.load() || _applyDepth != 0 || doorEntityId == 0) {
            return;
        }

        // C_LockBase also backs chests and containers, and the game raises
        // this for all of them.
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *entity       = entitySystem != nullptr ? entitySystem->GetEntity(doorEntityId) : nullptr;
        NativeDoorState door;
        if (entity == nullptr || entity->GetGuid() == 0 || !ResolveDoor(entity->GetGuid(), door)) {
            return;
        }

        // SetLocked(false) raised the unlock just before this callback.
        ClaimNativeEdges(doorEntityId);

        // No animation depends on a pick, so it carries no side.
        if (!SendLocalInteraction(entity->GetGuid(), Shared::RPC::DoorEvent::Lockpicked, door.direction > 0.0F, false, false, Shared::RPC::DoorActorAction::None, {})) {
            _strayedDoors.push_back(entity->GetGuid());
        }
    }

    bool DoorService::IsUsable(SDK::EntityId doorEntityId) {
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(doorEntityId) : nullptr;
        const Door *replica  = entity != nullptr ? Find(entity->GetGuid()) : nullptr;
        return replica == nullptr || replica->usable;
    }

    bool DoorService::ResolveInteractionTypes() {
        // Numbered by interaction_filter.xml's order when the filter loads,
        // so looked up by name in the filter's own registry. A prompt is only
        // registered once the filter has loaded, so the first answer is final
        // and every later registration reads it.
        if (!_interactionTypesLooked) {
            _interactionTypesLooked = true;
            _openType               = SDK::wh::entitymodule::FindInteractionType("doorOpen");
            _closeType              = SDK::wh::entitymodule::FindInteractionType("doorClose");
            _unlockType             = SDK::wh::entitymodule::FindInteractionType("doorUnlock");
            _lockpickType           = SDK::wh::entitymodule::FindInteractionType("doorLockpick");
            _interactionTypesFound  = _openType != SDK::wh::entitymodule::E_InteractionType::None && _closeType != SDK::wh::entitymodule::E_InteractionType::None && _unlockType != SDK::wh::entitymodule::E_InteractionType::None && _lockpickType != SDK::wh::entitymodule::E_InteractionType::None;
            if (!_interactionTypesFound) {
                Framework::Logging::GetLogger(kLogger)->error("interaction_filter.xml has no doorOpen/doorClose/doorUnlock/doorLockpick; client doorInteract is off");
            }
        }
        return _interactionTypesFound;
    }

    void DoorService::OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::E_InteractionType interactionType, SDK::HScriptFunction function) {
        if (function == 0 || !ResolveInteractionTypes()) {
            return;
        }
        if (interactionType != _openType && interactionType != _closeType && interactionType != _unlockType && interactionType != _lockpickType) {
            return;
        }

        // A door offers each of its four prompts at most once, so a prompt
        // registered again replaces its handler rather than adding one.
        const SDK::EntityId target = interactor.GetTargetEntityId();
        if (target != _promptTargetEntityId) {
            _promptTargetEntityId = target;
            _promptHandlers.clear();
        }
        for (RegisteredPrompt &prompt : _promptHandlers) {
            if (prompt.interactionType == interactionType) {
                prompt.function = function;
                return;
            }
        }
        _promptHandlers.push_back(RegisteredPrompt {function, interactionType});
    }

    bool DoorService::InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation) {
        if (!_captureLocalEvents.load() || invocation.m_targetEntityId == 0 || invocation.m_targetEntityId != _promptTargetEntityId) {
            return false;
        }
        const auto prompt = std::find_if(_promptHandlers.begin(), _promptHandlers.end(), [&invocation](const RegisteredPrompt &candidate) {
            return candidate.function == invocation.m_function;
        });
        if (prompt == _promptHandlers.end()) {
            return false;
        }

        NativeDoorState native;
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr ? entitySystem->GetEntity(invocation.m_targetEntityId) : nullptr;
        if (entity == nullptr || entity->GetGuid() == 0 || !ResolveDoor(entity->GetGuid(), native, nullptr, false)) {
            return false;
        }
        Door *replica = Find(entity->GetGuid());
        if (replica != nullptr && !replica->usable) {
            return true;
        }

        const Shared::Game::DoorPress press = Shared::Game::EvaluateDoorPress(prompt->interactionType == _lockpickType, native.direction, native.locked);
        if (press.actionCount == 0) {
            return false;
        }

        KCDC::Scripting::Events::DoorUse use;
        use.networkId      = replica != nullptr ? replica->GetNetworkID() : 0;
        use.guid           = entity->GetGuid();
        use.name           = BoundedText(entity->GetName(), Shared::RPC::kMaxDoorNameLength);
        use.open           = native.direction > 0.0F;
        use.locked         = native.locked;
        const bool keySide = LocalPlayerOnKeySide(entity, replica);

        // Every change the press makes is put to the handlers, in order, as
        // the server puts a report's, and the first refusal ends it. Refused,
        // the handler never runs.
        bool accepted = true;
        for (std::size_t index = 0; index < press.actionCount && accepted; ++index) {
            accepted = KCDC::Scripting::Events::DoorInteract(use, Shared::Game::DoorActionName(press.actions[index]), keySide);
        }
        return !accepted;
    }

    bool DoorService::LocalPlayerOnKeySide(SDK::IEntity *entity, const Door *replica) const {
        auto *entitySystem         = SDK::IEntitySystem::GetInstance();
        const SDK::IEntity *player = entitySystem != nullptr ? entitySystem->GetEntity(KCDC::Common::LocalActorEntityId()) : nullptr;
        if (player == nullptr) {
            return false;
        }

        // The rule the server's verdict reads: the level's own record, or for
        // a hung door the one the server built it from -- where it hangs, its
        // own facing, a keyhole that is never reversed.
        Shared::Game::DoorPlacement placement {};
        if (const Shared::Game::DoorPlacement *catalogued = Shared::Game::FindDoorPlacement(_observedLevel.c_str(), entity->GetGuid())) {
            placement = *catalogued;
        }
        else if (replica != nullptr && replica->IsSpawned()) {
            const glm::vec2 forward = Shared::Game::HungDoorForward(replica->spawned.rotation);
            placement.positionX     = replica->position.x;
            placement.positionY     = replica->position.y;
            placement.forwardX      = forward.x;
            placement.forwardY      = forward.y;
            placement.lockType      = replica->spawned.lockType;
            placement.lockReversed  = false;
        }
        else {
            return false;
        }
        const SDK::Vec3 actor = player->GetWorldPos();
        return Shared::Game::IsDoorKeySide(placement, Shared::Game::DoorFrontDistance(placement, actor.x, actor.y) >= 0.0F);
    }

    void DoorService::SendPendingTransitions() {
        for (auto it = _pendingLocalTransitions.begin(); it != _pendingLocalTransitions.end();) {
            const SDK::EntityId doorEntityId = it->first;
            ++it;
            SendPendingTransition(doorEntityId);
        }
    }

    bool DoorService::SendPendingTransition(SDK::EntityId doorEntityId) {
        const auto pending = _pendingLocalTransitions.find(doorEntityId);
        if (pending == _pendingLocalTransitions.end()) {
            return false;
        }

        // Read a tick after the use, once Lua has named the clip. A key turned
        // in the same use has already unlocked the door, so the report
        // carries the unlock with the push.
        PendingLocalTransition &transition = pending->second;
        NativeDoorState door;
        const char *deferral    = nullptr;
        bool nativeStillRunning = true;
        if (ResolveDoor(transition.doorGuid, door, &deferral)) {
            nativeStillRunning = door.inUse != 0.0F;
            if (door.lastAnimation.empty()) {
                deferral = "the native door has named no animation";
            }
            else if (transition.actorFence || CaptureLocalActorFence(transition.actorFence, &deferral)) {
                if (SendLocalInteraction(transition.doorGuid, Shared::RPC::DoorEvent::Manipulation, door.direction > 0.0F, door.locked, transition.fromFront, transition.actorAction, door.lastAnimation, &transition.actorFence, transition.presentationSequence)) {
                    _pendingLocalTransitions.erase(pending);
                    return true;
                }
                deferral = "the presentation queue would not take it";
            }
        }

        if (nativeStillRunning && MafiaNet::GetTime() - transition.startedAt < kPendingTransitionDeadlineMs) {
            return false;
        }

        DiscardPendingTransition(doorEntityId, deferral);
        return false;
    }

    void DoorService::DiscardPendingTransition(SDK::EntityId doorEntityId, const char *reason) {
        const auto pending = _pendingLocalTransitions.find(doorEntityId);
        if (pending == _pendingLocalTransitions.end()) {
            return;
        }
        Framework::Logging::GetLogger(kLogger)->warn("Discarded the local door transition on entity {} (guid {:#x}), which goes back to the server's state: {}", doorEntityId, pending->second.doorGuid, reason);
        KCDC::Core::PresentationModule::Get().CancelLocalDoor(pending->second.presentationSequence);
        _strayedDoors.push_back(pending->second.doorGuid);
        _pendingLocalTransitions.erase(pending);
    }

    // Every refusal is named, because this is the gate a door report is most
    // likely to die at and the reason is not recoverable from anything else
    // in the log: the replicated combat fields it compares are also what the
    // server re-checks, and a stale one of them looks exactly like a neutral
    // player who is simply not being heard.
    bool DoorService::CaptureLocalActorFence(LocalActorFence &actorFence, const char **failureReason) const {
        KCDC::Features::Player::Human *owner = KCDC::Features::Player::Human::FindLocal();
        if (owner == nullptr) {
            return Refuse(failureReason, "this client has no replica of its own body");
        }
        if (!owner->soul.valid || owner->soul.incarnation == 0) {
            return Refuse(failureReason, "the local soul snapshot has not been published");
        }
        if (owner->combatRevision == 0) {
            return Refuse(failureReason, "the local body carries no combat revision");
        }
        const KCDC::Features::Player::PlayerService::LocalCombat &localCombat = KCDC::Core::gApplication->GetPlayerService().Combat();
        if (localCombat.combatRevision != owner->combatRevision) {
            return Refuse(failureReason, "the replicated combat revision lags the combat hooks");
        }
        if (localCombat.fistsUp != owner->fistsUp) {
            return Refuse(failureReason, "the replicated fists-up bit lags the combat hooks");
        }
        if (localCombat.guardRequest != owner->guardRequest) {
            return Refuse(failureReason, "the replicated guard request lags the combat hooks");
        }
        if (localCombat.combatZone != owner->combatZone) {
            return Refuse(failureReason, "the replicated combat zone lags the combat hooks");
        }
        actorFence.incarnation    = owner->soul.incarnation;
        actorFence.stateEpoch     = owner->stateEpoch;
        actorFence.combatRevision = owner->combatRevision;
        return true;
    }

    bool DoorService::SendLocalInteraction(SDK::EntityGUID doorGuid, Shared::RPC::DoorEvent event, bool open, bool locked, bool fromFront, Shared::RPC::DoorActorAction actorAction, const std::string &objectAnimation, const LocalActorFence *actorFence,
        std::uint64_t presentationSequence) {
        if (doorGuid == 0 || _observedLevelHash == 0) {
            return false;
        }

        LocalActorFence capturedActorFence;
        if (actorFence != nullptr) {
            capturedActorFence = *actorFence;
        }
        else if (!CaptureLocalActorFence(capturedActorFence)) {
            return false;
        }
        if (!capturedActorFence) {
            return false;
        }

        Shared::RPC::DoorInteraction interaction;
        interaction.objectAnimation     = objectAnimation;
        interaction.doorGuid            = doorGuid;
        interaction.levelHash           = _observedLevelHash;
        interaction.actorIncarnation    = capturedActorFence.incarnation;
        interaction.actorStateEpoch     = capturedActorFence.stateEpoch;
        interaction.actorCombatRevision = capturedActorFence.combatRevision;
        interaction.event               = event;
        interaction.actorAction         = actorAction;
        interaction.open                = open;
        interaction.locked              = locked;
        interaction.fromFront           = fromFront;

        const bool queued = presentationSequence != 0 ? KCDC::Core::PresentationModule::Get().CompleteLocalDoor(presentationSequence, interaction) : KCDC::Core::PresentationModule::Get().QueueLocalDoor(interaction);
        if (!queued) {
            Framework::Logging::GetLogger(kLogger)->warn("Cannot queue local door {} event: guid={:#x}, reservation={}, action={}, animation='{}'", DoorEventName(event), doorGuid, presentationSequence, static_cast<unsigned>(actorAction), objectAnimation);
            return false;
        }
        return true;
    }

    void DoorService::PrepareObjectAnimation(SDK::EntityId doorEntityId, SDK::EntityId &userEntityId, bool opening, bool &accessedFromFront) {
        if (_applyDepth == 0 || doorEntityId != _animationDoorEntityId || opening != _animationOpening) {
            return;
        }

        userEntityId      = _animationUserEntityId;
        accessedFromFront = _animationAccessedFromFront;
    }

    void DoorService::Reset() {
        _acceptIncoming.store(false);
        _captureLocalEvents.store(false);
        {
            std::lock_guard lock(_incomingMutex);
            _incomingNearestQueries.clear();
        }
        {
            std::scoped_lock lock(_initializedDoorsMutex);
            _initializedDoors.clear();
        }

        for (const auto &transition : _pendingLocalTransitions) {
            KCDC::Core::PresentationModule::Get().CancelLocalDoor(transition.second.presentationSequence);
        }
        _pendingLocalTransitions.clear();
        _accessSide = {};
        {
            std::scoped_lock nativeEdgesLock(_nativeEdgesMutex);
            _nativeEdges.clear();
        }
        _strayedDoors.clear();
        _replicaIndex.clear();
        _observedContext   = 0;
        _observedLevelHash = 0;
        _observedLevel.clear();
        _applyDepth            = 0;
        _animationDoorEntityId = 0;
        _animationUserEntityId = 0;
        _promptTargetEntityId  = 0;
        _promptHandlers.clear();
    }
} // namespace KCDC::Features::Door
