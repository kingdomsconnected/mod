#include "stash.h"
#include "features/items/custom_item_module.h"
#include "stash_service.h"

#include "core/application.h"
#include "common/module_support.h"

#include "features/player/player_inventory_service.h"

#include "shared/features/inventory/inventory_sync.h"
#include "shared/features/stash/stash_inventory_message.h"
#include "shared/features/stash/stash_rpc.h"

#include "sdk/cryengine/animation/cry_char_animation_params.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_stash.h"
#include "sdk/wh/entitymodule/c_stash_manager.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace KCDC::Features::Stash {
    namespace {
        constexpr const char *kLogger = "Stash";

        /** A spawn that cannot succeed must not be retried every frame. */
        constexpr std::uint8_t kMaxMaterializeAttempts = 3;

        // AnimStash.lua places its lid on character slot/layer zero. These
        // are the existing TrackView flags used by the gate presentation.
        constexpr int kLidCharacterSlot            = 0;
        constexpr std::uint32_t kLidAnimationLayer = 0;
        constexpr std::uint32_t kLidDrivenFlags    = SDK::CA_TRACK_VIEW_EXCLUSIVE | SDK::CA_ALLOW_ANIM_RESTART | SDK::CA_LOOP_ANIMATION | SDK::CA_FORCE_SKELETON_UPDATE;

        /** A transfer the server never answered is given up after this. */
        constexpr std::uint64_t kTransferTimeoutMs = 10000;

        /** Well inside the server's two-minute view timeout. */
        constexpr std::uint64_t kViewRenewalMs = 30000;

        std::uint64_t NowMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        /** The server's row as the projection takes it: class, amount and properties. */
        bool ToProjectionRow(const nlohmann::json &row, std::string &id, nlohmann::json &projected) {
            if (!row.is_object() || !row.contains("id") || !row.at("id").is_string()) {
                return false;
            }
            id        = row.at("id").get<std::string>();
            projected = {{"item", row.value("item", nlohmann::json())}, {"amount", row.value("amount", nlohmann::json())}, {"metadata", row.value("metadata", nlohmann::json::object())}};
            return KCDC::Shared::Inventory::IsSyncRow(id, projected);
        }
    } // namespace

    void Stash::DeallocReplica(MafiaNet::Connection_RM3 *) {
        CleanupNative();
        delete this;
    }

    SDK::wh::entitymodule::C_Stash *Stash::GetNativeStash() const {
        auto *system = SDK::IEntitySystem::GetInstance();
        auto *entity = system != nullptr && _entityId != 0 ? system->GetEntity(_entityId) : nullptr;
        return entity != nullptr && entity->GetGuid() == GetEntityGuid() && SDK::wh::entitymodule::FindStashExtension(_entityId) == _nativeStash ? _nativeStash : nullptr;
    }

    bool Stash::TryBind() {
        if (isVirtual) {
            return TryCreateVirtualInventory();
        }
        // By GUID, for both kinds: the entity system's look-up is hashed, and
        // an engine that removed the entity -- a level container streaming
        // out, anything at a level switch -- is noticed the same frame rather
        // than left behind as a dangling pointer.
        auto *entitySystem           = SDK::IEntitySystem::GetInstance();
        const SDK::EntityId entityId = entitySystem != nullptr ? entitySystem->FindEntityByGuid(GetEntityGuid()) : 0;

        // An id is salted per spawn, so the same id is the same entity and
        // the extension walk can be skipped.
        if (entityId != 0 && entityId == _entityId && _nativeStash != nullptr) {
            return true;
        }
        SDK::wh::entitymodule::C_Stash *nativeStash = entityId != 0 ? SDK::wh::entitymodule::FindStashExtension(entityId) : nullptr;
        if (nativeStash != _nativeStash) {
            // Whatever was applied went with the old entity, inventory included.
            if (_openHere) {
                SendClosed();
            }
            ReleaseLidControl();
            ResetApplied();
            _materializeAttempts   = 0;
            _materializationFailed = false;
            _nativeStash           = nativeStash;
            _entityId              = nativeStash != nullptr ? nativeStash->GetEntityId() : 0;
        }

        // A level container is the level's to build; a spawned one is ours.
        if (_nativeStash == nullptr && levelGuid == 0) {
            return TryMaterialize();
        }
        return _nativeStash != nullptr;
    }

    bool Stash::TryMaterialize() {
        if (_materializationFailed || stashId == 0) {
            return false;
        }
        if (_materializeAttempts >= kMaxMaterializeAttempts) {
            _materializationFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("Gave up building stash {} after {} attempts", GetNetworkID(), kMaxMaterializeAttempts);
            return false;
        }
        ++_materializeAttempts;

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return false;
        }
        auto *registry = entitySystem->GetClassRegistry();
        if (registry == nullptr) {
            return false;
        }
        SDK::IEntityClass *entityClass = registry->FindClass("Stash");
        if (entityClass == nullptr) {
            _materializationFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("The 'Stash' entity class is not registered; no container can be built");
            return false;
        }

        const SDK::EntityGUID guid = GetEntityGuid();

        auto *stashManager = SDK::wh::entitymodule::C_StashManager::GetInstance();
        if (stashManager != nullptr && stashManager->FindByEntityGuid(guid) != nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("EntityGuid {:#x} for stash {} is already taken; retrying", guid, GetNetworkID());
            return false;
        }

        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Stash_%u", KCDC::Core::gApplication->GetStashService().NextSpawnOrdinal());

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.guid        = guid;
        params.name        = name;
        params.position    = SDK::Vec3 {position.x, position.y, position.z};
        params.rotation    = SDK::Quat {rotation.x, rotation.y, rotation.z, rotation.w};

        // A model is set between spawn and init, before AnimStash.lua's
        // OnSpawn loads `object_Model`, as the prop spawner does. Init failure
        // returns null and queues the entity for deletion, which is the signal
        // a duplicate guid or a refusing script produces.
        SDK::IEntity *entity = entitySystem->SpawnEntity(params, model.empty());
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("SpawnEntity refused the container for stash {}", GetNetworkID());
            return false;
        }
        if (!model.empty()) {
            auto *proxy     = entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script));
            auto *script    = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
            auto properties = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
            if (properties == nullptr) {
                entitySystem->RemoveEntity(entity->GetId(), false);
                _materializationFailed = true;
                Framework::Logging::GetLogger(kLogger)->error("Stash {} has no Properties table to carry model '{}'", GetNetworkID(), model);
                return false;
            }
            properties->SetValue("object_Model", model.c_str());
            const SDK::EntityId spawnedId = entity->GetId();
            // InitEntity queues the entity for deletion on failure.
            if (!entitySystem->InitEntity(entity, params)) {
                Framework::Logging::GetLogger(kLogger)->warn("InitEntity refused stash {} with model '{}'", GetNetworkID(), model);
                return false;
            }
            entity = entitySystem->GetEntity(spawnedId);
            if (entity == nullptr || entity->IsGarbage()) {
                return false;
            }
        }

        _entityId    = entity->GetId();
        _nativeStash = SDK::wh::entitymodule::FindStashExtension(_entityId);
        if (_nativeStash == nullptr) {
            // Scenery with an inventory nobody can reach; take it back out.
            entitySystem->RemoveEntity(_entityId, true);
            _entityId = 0;
            Framework::Logging::GetLogger(kLogger)->warn("Entity for stash {} came up without a C_Stash extension", GetNetworkID());
            return false;
        }

        Framework::Logging::GetLogger(kLogger)->info("Built stash {} as entity {} (guid {:#x})", GetNetworkID(), _entityId, guid);
        return true;
    }

    void Stash::CleanupNative() {
        if (_openHere) {
            SendClosed();
        }
        else if (isVirtual && _virtualOpenRequested) {
            // The server granted a view this client never opened.
            SendClosedFor(GetNetworkID());
        }
        CloseVirtualScreen();
        if (auto *inventory = isVirtual ? BoundInventory() : nullptr) {
            KCDC::Features::Player::PlayerInventoryService::AuthorityProjection scope(KCDC::Core::gApplication->GetPlayerInventoryService());
            _projection.Clear();
            SDK::wh::entitymodule::C_InventoryManager::GetInstance()->DestroyInventory(inventory);
        }
        _virtualInventory     = nullptr;
        _virtualInventoryWuid = SDK::wh::framework::WUID {SDK::wh::framework::WUID::kInvalid};
        _virtualOpenRequested = false;
        ReleaseLidControl();
        _nativeLidPending = false;
        _lidFailureLogged = false;
        // Release destroys the inventory with the entity, which is correct
        // here: the server holds what was in it. A level container is the
        // level's, and is only let go of.
        if (_entityId != 0 && levelGuid == 0 && GetNativeStash() != nullptr) {
            if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
                entitySystem->RemoveEntity(_entityId, true);
            }
        }
        _entityId    = 0;
        _nativeStash = nullptr;
        ResetApplied();
    }

    SDK::wh::entitymodule::C_Inventory *Stash::BoundInventory() const {
        if (!isVirtual) {
            return _nativeStash != nullptr ? _nativeStash->GetInventory() : nullptr;
        }
        // The manager lives as long as the game modules, but the inventory it
        // handed out can still be destroyed under us; the WUID says whether.
        if (_virtualInventory == nullptr || SDK::wh::entitymodule::C_InventoryManager::GetInstance()->InvokeFindInventoryByWuidRaw(_virtualInventoryWuid) != _virtualInventory) {
            return nullptr;
        }
        return _virtualInventory;
    }

    bool Stash::TryCreateVirtualInventory() {
        if (BoundInventory() != nullptr) {
            return true;
        }
        if (!_virtualOpenRequested) {
            return false;
        }
        // Stock the engine dropped leaves nothing projected; rebuild it from the server's rows.
        if (_virtualInventory != nullptr) {
            _projection.Clear();
            _virtualScreenOpened = false;
            _projectionFailed    = false;
        }
        _virtualInventory = SDK::wh::entitymodule::C_InventoryManager::GetInstance()->GetOrCreateInventory(SDK::CryGUID {0, GetEntityGuid()}, "");
        if (_virtualInventory == nullptr) {
            return false;
        }
        _virtualInventoryWuid = _virtualInventory->GetWuid();
        _rowsApplied          = false;
        return true;
    }

    void Stash::OnVirtualOpen(const nlohmann::json &snapshot) {
        if (!isVirtual) {
            return;
        }
        OnServerContents(snapshot);
        // A repeated open for a screen already up only refreshes its rows.
        if (_virtualScreenOpened) {
            return;
        }
        _virtualOpenRequested = true;
        _virtualOpenDeadline  = NowMilliseconds() + kTransferTimeoutMs;
    }

    void Stash::CloseVirtualScreen() {
        if (!isVirtual) {
            return;
        }
        auto *screen    = SDK::wh::C_GlobalEnvironment::GetInstance()->GetUIItemTransfer();
        auto *inventory = BoundInventory();
        if (screen != nullptr && inventory != nullptr && screen->IsOpen() && screen->IsUsingInventory(inventory)) {
            screen->Close(false);
        }
        _virtualScreenOpened = false;
    }

    void Stash::UpdateVirtualScreen() {
        auto *screen    = SDK::wh::C_GlobalEnvironment::GetInstance()->GetUIItemTransfer();
        auto *inventory = BoundInventory();
        if (_virtualScreenOpened) {
            if (screen == nullptr || inventory == nullptr || !screen->IsOpen() || !screen->IsUsingInventory(inventory)) {
                _virtualScreenOpened = false;
                SetOpenHere(false, GetNetworkID());
            }
            return;
        }
        if (!_virtualOpenRequested) {
            return;
        }
        if (_projectionFailed || NowMilliseconds() >= _virtualOpenDeadline) {
            // A failed Lua call can still have attached our inventory. Close
            // that pane even when the successful-open flag was never set.
            CloseVirtualScreen();
            _virtualOpenRequested = false;
            SendClosedFor(GetNetworkID());
            return;
        }
        if (inventory == nullptr || !_rowsApplied || !_snapshotReceived || screen == nullptr || screen->IsOpen() || !KCDC::Core::gApplication->GetPlayerInventoryService().IsSynced()) {
            return;
        }
        auto *player  = KCDC::Common::LocalPlayer();
        auto *proxy   = player != nullptr ? player->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        auto *table   = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        auto actor    = table != nullptr ? table->GetTable("actor") : SDK::ScriptTablePtr {};
        auto *scripts = SDK::IScriptSystem::GetInstance();
        if (!actor || !scripts->BeginCallTableMethod(actor.get(), "OpenItemTransferStore")) {
            return;
        }
        scripts->PushFuncParamAny(SDK::ScriptAnyValue {actor.get()});
        // Entity zero suppresses the game's entity-close callback. The native
        // bridge still builds filters, input capture and both inventory panes.
        scripts->PushFuncParamAny(SDK::ScriptAnyValue::Handle(0));
        scripts->PushFuncParamAny(SDK::ScriptAnyValue::Handle(_virtualInventoryWuid.value));
        scripts->PushFuncParamAny(SDK::ScriptAnyValue {""});
        scripts->PushFuncParamAny(SDK::ScriptAnyValue {"@ui_open_stash"});
        if (scripts->EndCall() && screen->IsOpen() && screen->IsUsingInventory(inventory)) {
            _virtualOpenRequested = false;
            _virtualScreenOpened  = true;
            SetOpenHere(true, GetNetworkID());
        }
    }

    void Stash::ResetApplied() {
        _stateApplied         = false;
        _openHere             = false;
        _presentationOpenHere = false;
        _openedThroughNetwork = 0;
        _projection.Clear();
        _rows.clear();
        _revision         = 0;
        _snapshotReceived = false;
        _rowsApplied      = false;
        _projectionDirty  = false;
        _projectionFailed = false;
        _pendingSnapshot  = nullptr;
        _queuedUnits.clear();
        _transferInFlight = false;
        _transferDeadline = 0;
        _nativeLock.reset();
        _appliedLockDifficulty = KCDC::Shared::Entities::StashEntity::kNativeLockDifficulty;
        _appliedLockFanciness  = KCDC::Shared::Entities::StashEntity::LockFanciness::Native;
    }

    void Stash::ApplyState() {
        if (_nativeStash == nullptr) {
            return;
        }

        // SetLocked drives the entity's own Lua `Lock` / `Unlock`, and is a
        // no-op when the runtime flag already matches.
        if (!_stateApplied || _appliedLocked != locked) {
            _nativeStash->SetLocked(locked);
            _appliedLocked = locked;
        }

        // Edges only, from the game's own lock: a container no script tuned
        // keeps its table untouched.
        if (_appliedLockDifficulty != lockDifficulty || _appliedLockFanciness != lockFanciness) {
            ApplyLockTuning();
        }

        // Whether it offers a lockpick is not applied here: the service
        // disables that prompt as the interactor registers it.
        _stateApplied = true;
    }

    void Stash::ApplyLockTuning() {
        // C_LockPicking::ReadLockProperties reads this table as the minigame
        // starts, and AnimStash.lua's prompt labels it, so the next pick and
        // the next prompt take it with no hook.
        auto *entity    = _nativeStash->GetEntity();
        auto *proxy     = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        auto *script    = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        auto properties = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        auto lock       = properties != nullptr ? properties->GetTable("Lock") : SDK::ScriptTablePtr {};

        // Taken as applied even without a table, so a miss is not retried every tick.
        _appliedLockDifficulty = lockDifficulty;
        _appliedLockFanciness  = lockFanciness;
        if (lock == nullptr) {
            return;
        }

        if (!_nativeLock.has_value()) {
            NativeLock native;
            const char *fanciness = nullptr;
            lock->GetValue("fLockDifficulty", native.difficulty);
            if (lock->GetValue("esLockFanciness", fanciness)) {
                native.fanciness = fanciness;
            }
            lock->GetValue("bLockDifficultyOverride", native.difficultyOverride);
            lock->GetValue("bLockFancinessOverride", native.fancinessOverride);
            _nativeLock = native;
        }

        using LockFanciness        = KCDC::Shared::Entities::StashEntity::LockFanciness;
        const bool tunedDifficulty = lockDifficulty >= 0.0F;
        lock->SetValue("fLockDifficulty", tunedDifficulty ? lockDifficulty : _nativeLock->difficulty);
        lock->SetValue("bLockDifficultyOverride", tunedDifficulty || _nativeLock->difficultyOverride);

        const bool tunedFanciness = lockFanciness != LockFanciness::Native;
        lock->SetValue("esLockFanciness", tunedFanciness ? (lockFanciness == LockFanciness::Fancy ? "Fancy" : "Common") : _nativeLock->fanciness.c_str());
        lock->SetValue("bLockFancinessOverride", tunedFanciness || _nativeLock->fancinessOverride);
    }

    void Stash::ReleaseLidControl() {
        // Resolve through the entity registry before touching a cached
        // character: unloading/replacing the model invalidates its pointer.
        auto *native = GetNativeStash();
        auto *entity = native != nullptr ? native->GetEntity() : nullptr;
        if (entity != nullptr && _lidCharacter != nullptr && entity->GetCharacter(kLidCharacterSlot) == _lidCharacter) {
            if (auto *skeleton = _lidCharacter->GetISkeletonAnim()) {
                // Shared open/close clips may otherwise reuse the stopped
                // driven entry when the next local native opening begins.
                if (_lidAnimationId >= 0) {
                    skeleton->ClearFIFOLayer(kLidAnimationLayer);
                }
                skeleton->SetTrackViewExclusive(0);
            }
        }
        // This belongs to the entity, not its replaceable character slot.
        // Preserve native update flags that our presentation did not acquire.
        if (entity != nullptr && _lidForcedUpdate) {
            entity->SetFlagsExtended(entity->GetFlagsExtended() & ~SDK::ENTITY_FLAG_EXTENDED_FORCE_UPDATE);
            entity->Activate(_lidWasActive);
        }
        _lidForcedUpdate = false;
        _lidWasActive    = false;
        _lidCharacter    = nullptr;
        _lidAnimationId  = -1;
        _lidOpenId       = -1;
        _lidCloseId      = -1;
        _lidMotion.Reset();
    }

    bool Stash::ResolveLidClips(SDK::ICharacterInstance *character) {
        _lidOpenId            = -1;
        _lidCloseId           = -1;
        auto *entity          = _nativeStash->GetEntity();
        auto *proxy           = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        auto *script          = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        auto *animations      = character != nullptr ? character->GetIAnimationSet() : nullptr;
        auto properties       = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        auto animation        = properties != nullptr ? properties->GetTable("Animation") : SDK::ScriptTablePtr {};
        const char *openClip  = nullptr;
        const char *closeClip = nullptr;
        if (animations == nullptr || animation == nullptr || !animation->GetValue("anim_Open", openClip) || !animation->GetValue("anim_Close", closeClip)) {
            return false;
        }
        _lidSameClip = closeClip[0] == '\0' || std::strcmp(openClip, closeClip) == 0;
        _lidOpenId   = openClip[0] != '\0' ? animations->GetAnimIDByName(openClip) : -1;
        _lidCloseId  = _lidSameClip ? _lidOpenId : animations->GetAnimIDByName(closeClip);
        return _lidOpenId >= 0 && _lidCloseId >= 0;
    }

    void Stash::ApplyLidPresentation() {
        if (_nativeStash == nullptr) {
            return;
        }
        // A closed chest nobody drives is the game's own: nothing to read and
        // no update forced on it. Presentation runs only while the lid is open
        // or still closing, then gives the entity back.
        if (!isOpen && _lidAnimationId < 0 && !_nativeLidPending) {
            return;
        }
        auto *entity = _nativeStash->GetEntity();
        auto *proxy  = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        auto *script = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        if (script == nullptr) {
            ReleaseLidControl();
            return;
        }

        // The local opener retains the game's actual UI and completion
        // callbacks. Observers never call Open/Close/DoPlayAnimation: those
        // methods report stock access, AI events and tutorial effects.
        if (_openHere || _presentationOpenHere) {
            return;
        }
        if (_nativeLidPending) {
            float needsUpdate = 0.0F;
            if (!script->GetValue("bNeedUpdate", needsUpdate) || needsUpdate != 0.0F) {
                return;
            }
            _nativeLidPending = false;
            if (!isOpen && _lidAnimationId < 0) {
                return;
            }
        }
        auto *character = entity->GetCharacter(kLidCharacterSlot);
        if (character != _lidCharacter || _lidOpenId < 0) {
            ReleaseLidControl();
            _lidCharacter = character;
            if (!ResolveLidClips(character)) {
                if (!_lidFailureLogged) {
                    Framework::Logging::GetLogger(kLogger)->warn("Container {} has no usable lid animation", GetNetworkID());
                    _lidFailureLogged = true;
                }
                ReleaseLidControl();
                return;
            }
        }
        auto *animations      = character->GetIAnimationSet();
        auto *skeleton        = character->GetISkeletonAnim();
        const int animationId = isOpen || _lidSameClip ? _lidOpenId : _lidCloseId;
        const auto openness   = skeleton != nullptr ? _lidMotion.Update(isOpen, MafiaNet::GetTimeUS() / 1000, animations->GetDuration_sec(animationId)) : std::nullopt;
        if (!openness.has_value()) {
            ReleaseLidControl();
            return;
        }
        if (!isOpen && *openness <= 0.0F) {
            ReleaseLidControl();
            return;
        }

        // AnimStash uses ENTITY_UPDATE_SCRIPT: Activate derives its decision
        // from the extended flags, ignoring the requested Boolean. Lock's
        // reset clears FORCE_UPDATE after two script updates, so reacquire it
        // when needed while we own presentation. Skeleton flags alone do not
        // schedule the entity's animation processing.
        const auto extendedFlags = entity->GetFlagsExtended();
        if ((extendedFlags & SDK::ENTITY_FLAG_EXTENDED_FORCE_UPDATE) == 0) {
            if (!_lidForcedUpdate) {
                _lidWasActive = entity->IsActive();
            }
            _lidForcedUpdate = true;
            entity->SetFlagsExtended(extendedFlags | SDK::ENTITY_FLAG_EXTENDED_FORCE_UPDATE);
            entity->Activate(true);
        }
        else if (!entity->IsActive()) {
            entity->Activate(true);
        }

        // Lock() resets the animation. ApplyState runs first, so this restores
        // the accepted open pose after a new holder locks observers out.
        skeleton->SetTrackViewExclusive(1);
        if (_lidAnimationId != animationId || skeleton->GetNumAnimsInFIFO(kLidAnimationLayer) != 1) {
            skeleton->ClearFIFOLayer(kLidAnimationLayer);
            SDK::CryCharAnimationParams params(static_cast<std::int32_t>(kLidAnimationLayer), kLidDrivenFlags);
            params.m_fTransTime     = 0.0F;
            params.m_fPlaybackSpeed = 0.0F;
            if (!skeleton->StartAnimationById(animationId, params)) {
                ReleaseLidControl();
                return;
            }
            _lidAnimationId = animationId;
        }
        const float normalizedTime = isOpen || _lidSameClip ? *openness : 1.0F - *openness;
        // Deliberately do not fire clip events or alter the Lua interaction
        // flags. A visual open chest remains locked to every non-holder.
        skeleton->SetLayerNormalizedTime(kLidAnimationLayer, normalizedTime);
    }

    void Stash::OnServerContents(const nlohmann::json &snapshot) {
        const auto revision = snapshot.value("revision", std::uint64_t(0));
        if (!_pendingSnapshot.is_null() && revision < _pendingSnapshot.value("revision", std::uint64_t(0))) {
            return;
        }
        _pendingSnapshot = snapshot;
    }

    void Stash::SetPresentationOpenHere(bool openHere) {
        if (_presentationOpenHere == openHere) {
            return;
        }
        if (openHere) {
            ReleaseLidControl();
        }
        _presentationOpenHere = openHere;
        _nativeLidPending     = true;
    }

    void Stash::SetOpenHere(bool openHere, std::uint64_t physicalNetworkId) {
        if (_openHere == openHere) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (openHere) {
            // OnOpened reports before AnimStash starts the local open clip.
            // Remove our stopped entry so shared-clip chests restart normally.
            ReleaseLidControl();
            _openedThroughNetwork = physicalNetworkId;
            _renewAt              = NowMilliseconds() + kViewRenewalMs;
            Shared::RPC::StashOpened opened;
            opened.stashNetworkId = physicalNetworkId;
            if (net != nullptr) {
                net->BroadcastRPC(opened, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
        else {
            // Take All asks for every stack and then the game closes the
            // screen in the same frame: what it asked for goes first.
            FlushTransfer();
            SendClosed();
        }
        _openHere         = openHere;
        _nativeLidPending = true;
        _queuedUnits.clear();
    }

    void Stash::SendClosed() {
        if (_openedThroughNetwork == 0) {
            return;
        }
        SendClosedFor(_openedThroughNetwork);
        _openedThroughNetwork = 0;
    }

    void Stash::SendClosedFor(std::uint64_t stashNetworkId) {
        Shared::RPC::StashClosed closed;
        closed.stashNetworkId = stashNetworkId;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(closed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void Stash::RetireView() {
        if (isVirtual) {
            CleanupNative();
            return;
        }
        if (_openHere) {
            SendClosed();
        }
        SetPresentationOpenHere(false);
        _openHere = false;
        _queuedUnits.clear();
    }

    void Stash::ServerClosed() {
        if (isVirtual) {
            _virtualOpenRequested = false;
            CloseVirtualScreen();
        }
        if (!_openHere) {
            return;
        }
        _openHere             = false;
        _openedThroughNetwork = 0;
        _queuedUnits.clear();
        _transferInFlight = false;
        if (!isVirtual) {
            KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction("This container was closed. Close the screen and open it again.");
        }
    }

    void Stash::ApplyContents() {
        auto *inventory = BoundInventory();
        if (inventory == nullptr || _projectionFailed) {
            return;
        }
        if (!_pendingSnapshot.is_null()) {
            const auto snapshot = std::exchange(_pendingSnapshot, nlohmann::json());
            std::map<std::string, nlohmann::json> rows;
            for (const auto &row : snapshot.value("rows", nlohmann::json::array())) {
                std::string id;
                nlohmann::json projected;
                if (!ToProjectionRow(row, id, projected)) {
                    Framework::Logging::GetLogger(kLogger)->error("Container {} snapshot carries a row that cannot be shown", GetNetworkID());
                    return;
                }
                rows.emplace(std::move(id), std::move(projected));
            }
            _rows             = std::move(rows);
            _revision         = snapshot.value("revision", std::uint64_t(0));
            _snapshotReceived = true;
            _rowsApplied      = false;
            _transferInFlight = false;
            const auto code   = snapshot.value("code", std::string());
            if (!code.empty()) {
                Framework::Logging::GetLogger(kLogger)->warn("Container {} transfer refused: {}", GetNetworkID(), code);
                KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction("The container refused that transfer.");
            }
        }
        // Until the server's rows arrive the container shows nothing: what the
        // game put in it natively is never offered.
        if (_rowsApplied && !_projectionDirty) {
            return;
        }
        KCDC::Features::Player::PlayerInventoryService::AuthorityProjection scope(KCDC::Core::gApplication->GetPlayerInventoryService());
        std::string error;
        std::map<std::string, std::string> unshown;
        if (!KCDC::Features::Items::CustomItemModule::Get().Ready()) {
            return;
        }
        for (const auto &[id, row] : _rows) {
            if (KCDC::Shared::Game::FindItemClass(row.at("item").get<std::string>()) == nullptr) {
                return;
            }
        }
        if (!_projection.Apply(inventory, _rows, unshown, error)) {
            _projectionFailed = true;
            Framework::Logging::GetLogger(kLogger)->error("Container {} projection refused: {}", GetNetworkID(), error);
            return;
        }
        for (const auto &[id, reason] : unshown) {
            Framework::Logging::GetLogger(kLogger)->error("Container {} row {} could not be shown: {}", GetNetworkID(), id, reason);
        }
        _rowsApplied     = true;
        _projectionDirty = false;
    }

    bool Stash::RequestTransfer(const std::vector<std::pair<SDK::wh::entitymodule::C_Item *, std::uint32_t>> &items, bool deposit) {
        auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (items.empty() || !_openHere || !_snapshotReceived || _transferInFlight || _projectionFailed || !inventory.IsSynced()) {
            return false;
        }
        if (!_queuedUnits.empty() && _queuedDeposit != deposit) {
            return false;
        }
        auto staged = _queuedUnits;
        for (const auto &[item, amount] : items) {
            if (item == nullptr || amount == 0) {
                return false;
            }
            auto remaining = amount;
            for (const auto &unit : deposit ? inventory.UnitsOf(item->GetId()) : _projection.UnitsOf(item->GetId())) {
                if (remaining == 0) {
                    break;
                }
                const auto take = std::min(remaining, unit.amount);
                staged[unit.id] += take;
                remaining -= take;
            }
            if (remaining != 0 || staged.size() > Shared::RPC::kMaxStashItems) {
                return false;
            }
        }
        _queuedUnits   = std::move(staged);
        _queuedDeposit = deposit;
        return true;
    }

    void Stash::FlushTransfer() {
        if (_transferInFlight && _transferDeadline <= NowMilliseconds()) {
            _transferInFlight = false;
        }
        if (_queuedUnits.empty() || _transferInFlight) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || _openedThroughNetwork == 0) {
            _queuedUnits.clear();
            return;
        }
        nlohmann::json units = nlohmann::json::array();
        for (const auto &[id, amount] : _queuedUnits) {
            units.push_back({{"id", id}, {"amount", amount}});
        }
        Shared::RPC::StashInventoryTransfer request;
        request.body = nlohmann::json({{"stash", _openedThroughNetwork}, {"deposit", _queuedDeposit}, {"units", std::move(units)}, {"playerRevision", KCDC::Core::gApplication->GetPlayerInventoryService().SyncedRevision()}, {"stashRevision", _revision}}).dump();
        net->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        _queuedUnits.clear();
        _transferInFlight = true;
        _transferDeadline = NowMilliseconds() + kTransferTimeoutMs;
    }

    void Stash::Update() {
        if (!TryBind()) {
            // An open whose stock cannot bind must still time out and release its grant.
            if (isVirtual) {
                UpdateVirtualScreen();
            }
            return;
        }
        // A linked child opens its master's inventory, which the master's
        // replica keeps; it has none of its own to fill.
        if (OwnsContents()) {
            ApplyContents();
        }
        if (isVirtual) {
            UpdateVirtualScreen();
        }
        ApplyState();
        ApplyLidPresentation();
        FlushTransfer();
        // The open again: the server keeps the view of a holder whose screen is still up.
        if (_openHere && _openedThroughNetwork != 0 && NowMilliseconds() >= _renewAt) {
            _renewAt = NowMilliseconds() + kViewRenewalMs;
            Shared::RPC::StashOpened opened;
            opened.stashNetworkId = _openedThroughNetwork;
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->BroadcastRPC(opened, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
    }
} // namespace KCDC::Features::Stash
