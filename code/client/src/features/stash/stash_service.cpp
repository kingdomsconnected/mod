#include "stash_service.h"

#include "core/application.h"
#include "common/module_support.h"

#include "shared/features/stash/stash_rpc.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_inventory_manager.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "sdk/wh/entitymodule/c_stash.h"

#include "features/ground_item/ground_item_service.h"
#include "features/player/player_inventory_service.h"
#include "shared/features/stash/stash_inventory_message.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/hooking/hooking.h>

#include <mafianet/get_time.h>

#include <algorithm>

namespace KCDC::Features::Stash {
    namespace {
        constexpr const char *kLogger = "Stash";

        /** A press the server never answered is dropped rather than run late. */
        constexpr MafiaNet::Time kPendingTimeoutMs = 5000;

        /** Matches the server's lockpick grant: the minigame can run long. */
        constexpr MafiaNet::Time kLockpickGrantTimeoutMs = 180000;

        /** One answer per press, so this only bounds a server that answers questions nobody asked. */
        constexpr std::size_t kMaxIncomingVerdicts = 64;

        Stash *FindStashReplica(std::uint64_t networkId) {
            auto *replication = Framework::CoreModules::GetReplication();
            return replication != nullptr && networkId != 0 ? replication->GetEntity<Stash>(networkId) : nullptr;
        }

        void SendLockReport(std::uint64_t stashNetworkId, Shared::RPC::StashLockEvent event) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (net == nullptr) {
                return;
            }
            Shared::RPC::StashLockReport report;
            report.stashNetworkId = stashNetworkId;
            report.event          = event;
            net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }

        void SendVirtualClosed(std::uint64_t stashNetworkId) {
            Shared::RPC::StashClosed closed;
            closed.stashNetworkId = stashNetworkId;
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->BroadcastRPC(closed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
    } // namespace

    void StashService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        net->RegisterRPC<Shared::RPC::StashInventorySnapshot>([](const auto &snapshot, MafiaNet::Packet *) {
            if (!snapshot.valid || KCDC::Core::gApplication == nullptr) {
                return;
            }
            KCDC::Core::gApplication->GetStashService().QueueSnapshot(snapshot.body, IncomingMessageKind::Snapshot);
        });
        net->RegisterRPC<Shared::RPC::StashVirtualOpen>([](const auto &open, MafiaNet::Packet *) {
            if (!open.valid || KCDC::Core::gApplication == nullptr) {
                return;
            }
            KCDC::Core::gApplication->GetStashService().QueueSnapshot(open.body, IncomingMessageKind::VirtualOpen);
        });
        net->RegisterRPC<Shared::RPC::StashClosed>([](const Shared::RPC::StashClosed &closed, MafiaNet::Packet *) {
            if (closed.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetStashService().QueueServerClose(closed.stashNetworkId);
            }
        });
        net->RegisterRPC<Shared::RPC::StashInteractVerdict>([](const Shared::RPC::StashInteractVerdict &verdict, MafiaNet::Packet *) {
            if (verdict.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetStashService().QueueVerdict(verdict);
            }
        });
    }

    Stash *StashService::FindReplica(const SDK::wh::entitymodule::C_Stash *nativeStash) const {
        if (nativeStash != nullptr) {
            for (const BoundStash &bound : _bound) {
                if (bound.nativeStash == nativeStash) {
                    return FindStashReplica(bound.networkId);
                }
            }
        }
        return nullptr;
    }

    Stash *StashService::FindReplica(SDK::EntityId entityId) const {
        if (entityId != 0) {
            for (const BoundStash &bound : _bound) {
                if (bound.entityId == entityId) {
                    return FindStashReplica(bound.networkId);
                }
            }
        }
        return nullptr;
    }

    Stash *StashService::ContentsReplica(Stash *stash) const {
        if (stash == nullptr || stash->OwnsContents()) {
            return stash;
        }
        return FindStashReplica(stash->contentsNetworkId);
    }

    void StashService::RefreshIndex() {
        _bound.clear();
        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> projected;
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        // Each replica bound its native container in this tick's update.
        replication->ForEach<Stash>([this, &projected](Stash *stash) {
            auto *native    = stash->BoundNativeStash();
            auto *inventory = stash->BoundInventory();
            if (inventory == nullptr) {
                return;
            }
            _bound.push_back(BoundStash {native, inventory, stash->GetNativeEntityId(), stash->GetNetworkID()});
            if (auto *projection = stash->ProjectedInventory()) {
                projected.push_back(projection);
            }
        });
        std::scoped_lock lock(_projectedMutex);
        _projected = std::move(projected);
    }

    void StashService::NotifyNativeChange(const SDK::wh::entitymodule::C_ItemHolder *holder) {
        if (holder == nullptr) {
            return;
        }
        std::scoped_lock lock(_projectedMutex);
        if (std::find(_projected.begin(), _projected.end(), holder) != _projected.end() && std::find(_changedHolders.begin(), _changedHolders.end(), holder) == _changedHolders.end()) {
            _changedHolders.push_back(holder);
        }
    }

    bool StashService::ManagesNativeItem(const SDK::wh::entitymodule::C_Item *item) const {
        const auto *owner = item != nullptr ? item->GetOwner() : nullptr;
        if (owner == nullptr) {
            return false;
        }
        std::scoped_lock lock(_projectedMutex);
        return std::find(_projected.begin(), _projected.end(), owner) != _projected.end();
    }

    bool StashService::AnswerHasVisibleItems(std::uint64_t inventoryWuid, bool &answer) const {
        auto *manager   = SDK::wh::entitymodule::C_InventoryManager::GetInstance();
        auto *inventory = manager != nullptr ? manager->InvokeFindInventoryByWuidRaw(SDK::wh::framework::WUID {inventoryWuid}) : nullptr;
        if (inventory == nullptr) {
            return false;
        }
        for (const BoundStash &bound : _bound) {
            if (bound.inventory != inventory) {
                continue;
            }
            const Stash *stash = FindStashReplica(bound.networkId);
            if (stash == nullptr) {
                return false;
            }
            // Despite its name the native answers "nothing to show": AnimStash.lua
            // Stash:GetActions appends ui_empty_stash when it returns true.
            answer = !stash->hasContents;
            return true;
        }
        return false;
    }

    void StashService::QueueSnapshot(const std::string &body, IncomingMessageKind kind) {
        auto snapshot = nlohmann::json::parse(body, nullptr, false);
        if (!snapshot.is_object() || !snapshot.contains("stash") || !snapshot.at("stash").is_number_unsigned() || snapshot.at("stash").get<std::uint64_t>() == 0) {
            return;
        }
        const auto stashNetworkId = snapshot.at("stash").get<std::uint64_t>();
        bool queued               = false;
        {
            std::scoped_lock lock(_incomingMutex);
            if (_incoming.size() < 64) {
                _incoming.push_back(IncomingMessage {kind, stashNetworkId, std::move(snapshot)});
                queued = true;
            }
        }
        if (!queued && kind == IncomingMessageKind::VirtualOpen) {
            SendVirtualClosed(stashNetworkId);
        }
    }

    void StashService::QueueServerClose(std::uint64_t stashNetworkId) {
        std::scoped_lock lock(_incomingMutex);
        // A close supersedes queued contents and opens for this same view.
        // Snapshot traffic must never make an access revocation disappear.
        std::erase_if(_incoming, [stashNetworkId](const IncomingMessage &message) {
            return message.stashNetworkId == stashNetworkId;
        });
        _incoming.push_back(IncomingMessage {IncomingMessageKind::Close, stashNetworkId, {}});
    }

    void StashService::OnNativeOpened(SDK::wh::entitymodule::C_Stash *native, bool byPlayer) {
        if (!byPlayer || native == nullptr) {
            return;
        }
        auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
        auto *physical  = FindReplica(native);
        auto *stash     = ContentsReplica(physical);
        if (stash == nullptr) {
            // A container the server does not replicate offers nothing.
            if (inventory.HasAuthority() && native->GetInventory() != nullptr) {
                KCDC::Features::Player::PlayerInventoryService::AuthorityProjection scope(inventory);
                native->GetInventory()->InvokeRemoveAllItemsRaw();
            }
            return;
        }
        if (physical != stash) {
            physical->SetPresentationOpenHere(true);
        }
        stash->SetOpenHere(true, physical->GetNetworkID());
    }

    void StashService::OnNativeClosed(SDK::wh::entitymodule::C_Stash *native) {
        auto *physical = FindReplica(native);
        auto *stash    = ContentsReplica(physical);
        if (physical != stash && physical != nullptr) {
            physical->SetPresentationOpenHere(false);
        }
        if (stash == nullptr || !stash->IsOpenHere()) {
            return;
        }
        stash->SetOpenHere(false, physical != nullptr ? physical->GetNetworkID() : 0);
    }

    bool StashService::OnTransferRequested(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount) {
        auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (!inventory.HasAuthority() || inventory.IsApplyingAuthority()) {
            return false;
        }
        if (destination == nullptr || items.IsEmpty() || amount == 0) {
            return true;
        }
        auto *local    = KCDC::Features::GroundItem::InventoryOf(KCDC::Common::LocalHumanActor());
        auto *source   = items[0] != nullptr ? items[0]->GetOwner() : nullptr;
        Stash *matched = nullptr;
        bool deposit   = false;
        for (const BoundStash &bound : _bound) {
            const bool toStash  = bound.inventory != nullptr && local != nullptr && destination == bound.inventory && source == local;
            const bool toPlayer = bound.inventory != nullptr && local != nullptr && destination == local && source == bound.inventory;
            if (toStash || toPlayer) {
                matched = ContentsReplica(FindStashReplica(bound.networkId));
                deposit = toStash;
                break;
            }
        }
        if (matched != nullptr) {
            // Match the game's finite selection iterator before it changes
            // ownership, provenance or counts. Never retain its vector.
            std::vector<std::pair<SDK::wh::entitymodule::C_Item *, std::uint32_t>> selected;
            for (auto *item : items) {
                if (amount == 0) {
                    break;
                }
                if (item == nullptr || item->GetOwner() != source || item->GetAmount() == 0 || selected.size() >= Shared::RPC::kMaxStashItems) {
                    inventory.RefuseUnsupportedAction("The selected container items have changed. Close and reopen the container.");
                    return true;
                }
                const auto count = std::min(amount, item->GetAmount());
                selected.emplace_back(item, count);
                amount -= count;
            }
            if (!matched->RequestTransfer(selected, deposit)) {
                inventory.RefuseUnsupportedAction("The container is busy or its contents have changed. Wait, then close and reopen it if needed.");
            }
            return true;
        }
        inventory.RefuseUnsupportedAction("This inventory transfer is not available on this server yet.");
        return true;
    }

    void StashService::OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::wh::entitymodule::E_InteractionType interactionType, SDK::HScriptFunction function) {
        // Every prompt in the game passes here; only a replicated container's
        // are ever looked up again.
        const Stash *stash = FindReplica(interactor.GetTargetEntityId());
        if (stash == nullptr || function == 0) {
            return;
        }
        _actionTypes.insert_or_assign(function, interactionType);

        // AnimStash.lua offers the pick whenever its own lock allows one; the
        // server's word goes on the record instead, which AddLuaAction reads
        // before it asks the interaction filter or keeps a copy.
        if (record.m_enabled && ResolveInteractionTypes() && interactionType == _lockpickType && !stash->lockpickable) {
            record.m_enabled   = false;
            record.m_uiVisible = false;
        }
    }

    bool StashService::ResolveInteractionTypes() {
        if (_openType != SDK::wh::entitymodule::E_InteractionType::None && _unlockType != SDK::wh::entitymodule::E_InteractionType::None && _lockpickType != SDK::wh::entitymodule::E_InteractionType::None) {
            return true;
        }

        // Numbered by interaction_filter.xml's order when the filter loads,
        // so looked up by name in the filter's own registry.
        _openType     = SDK::wh::entitymodule::FindInteractionType("stashOpen");
        _unlockType   = SDK::wh::entitymodule::FindInteractionType("stashUnlock");
        _lockpickType = SDK::wh::entitymodule::FindInteractionType("stashLockpick");

        const bool resolved = _openType != SDK::wh::entitymodule::E_InteractionType::None && _unlockType != SDK::wh::entitymodule::E_InteractionType::None && _lockpickType != SDK::wh::entitymodule::E_InteractionType::None;
        if (resolved) {
            Framework::Logging::GetLogger(kLogger)->info("Stash interaction types: open={} unlock={} lockpick={}", static_cast<std::uint32_t>(_openType), static_cast<std::uint32_t>(_unlockType), static_cast<std::uint32_t>(_lockpickType));
        }
        return resolved;
    }

    bool StashService::InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation) {
        if (_replayDepth > 0 || invocation.m_targetEntityId == 0) {
            return false;
        }

        Stash *stash = FindReplica(invocation.m_targetEntityId);
        if (stash == nullptr) {
            return false;
        }
        const auto registered = _actionTypes.find(invocation.m_function);
        if (registered == _actionTypes.end()) {
            return false;
        }
        if (!ResolveInteractionTypes()) {
            // Let it run: the server grants nothing it did not see asked for,
            // so an open that slips through gets neither lease nor contents.
            Framework::Logging::GetLogger(kLogger)->error("The interaction filter has no stash interactions; stash {} cannot be gated", stash->GetNetworkID());
            return false;
        }

        // The handler is the container's own Lua; holding it for the answer
        // takes a registry reference of our own through the script system.
        SDK::IScriptSystem *scriptSystem = SDK::IScriptSystem::GetInstance();
        if (scriptSystem == nullptr) {
            return false;
        }

        Shared::RPC::StashAction action;
        if (registered->second == _openType) {
            action = Shared::RPC::StashAction::Open;
        }
        else if (registered->second == _unlockType) {
            action = Shared::RPC::StashAction::Unlock;
        }
        else if (registered->second == _lockpickType) {
            action = Shared::RPC::StashAction::Lockpick;
        }
        else {
            // A close, and anything this gate does not know, runs at once.
            return false;
        }

        // A second press while the first is being asked about is the same
        // intent; it waits on the same answer.
        for (const auto &[requestId, pending] : _pending) {
            if (pending.stashNetworkId == stash->GetNetworkID()) {
                return true;
            }
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return true;
        }

        // The interactor's own reference goes when the prompt list is rebuilt,
        // which can happen before the answer lands.
        PendingInteraction pending;
        pending.stashNetworkId = stash->GetNetworkID();
        pending.entityId       = invocation.m_targetEntityId;
        pending.function       = scriptSystem->AddFuncRef(invocation.m_function);
        pending.scriptSystem   = scriptSystem;
        pending.action         = action;
        pending.expiresAt      = static_cast<std::uint64_t>(MafiaNet::GetTime() + kPendingTimeoutMs);
        if (pending.function == 0) {
            return true;
        }

        Shared::RPC::StashInteractRequest request;
        request.stashNetworkId = pending.stashNetworkId;
        request.requestId      = _nextRequestId++;
        request.action         = action;
        if (_nextRequestId == 0) {
            _nextRequestId = 1;
        }
        _pending.insert_or_assign(request.requestId, pending);
        net->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool StashService::AnswerCanUnlock(const SDK::wh::entitymodule::C_Stash *nativeStash, const SDK::CryGUID &levelKey, bool &answer) const {
        const Stash *stash = FindReplica(nativeStash);
        if (stash == nullptr) {
            return false;
        }

        // A script's key replaces the level's outright; a script's lock takes
        // only a real key, never the generated home or shop one.
        if (stash->keyItemClass.IsValid()) {
            answer = nativeStash->CanUnlockWithKey(KCDC::Common::ToCryGuid(stash->keyItemClass));
            return true;
        }
        if (stash->lockedByServer) {
            answer = nativeStash->CanUnlockWithKey(levelKey);
            return true;
        }
        return false;
    }

    void StashService::NotifyUnlocked(SDK::EntityId entityId) {
        if (_replayDepth == 0 || _replayEntityId != entityId || _replayAction != Shared::RPC::StashAction::Unlock) {
            return;
        }
        if (Stash *stash = FindReplica(entityId)) {
            SendLockReport(stash->GetNetworkID(), Shared::RPC::StashLockEvent::Unlocked);
        }
    }

    void StashService::NotifyLockpicked(SDK::EntityId entityId) {
        const auto grant = _lockpickGrants.find(entityId);
        if (grant == _lockpickGrants.end()) {
            return;
        }
        const bool live = grant->second > MafiaNet::GetTime();
        _lockpickGrants.erase(grant);
        if (!live) {
            return;
        }
        if (Stash *stash = FindReplica(entityId)) {
            SendLockReport(stash->GetNetworkID(), Shared::RPC::StashLockEvent::Lockpicked);
        }
    }

    void StashService::QueueVerdict(const Shared::RPC::StashInteractVerdict &verdict) {
        std::scoped_lock lock(_incomingMutex);
        if (_incomingVerdicts.size() < kMaxIncomingVerdicts) {
            _incomingVerdicts.push_back(verdict);
        }
    }

    void StashService::ApplyVerdicts() {
        std::vector<Shared::RPC::StashInteractVerdict> verdicts;
        {
            std::scoped_lock lock(_incomingMutex);
            verdicts.swap(_incomingVerdicts);
        }

        for (const Shared::RPC::StashInteractVerdict &verdict : verdicts) {
            const auto found = _pending.find(verdict.requestId);
            if (found == _pending.end() || found->second.stashNetworkId != verdict.stashNetworkId) {
                continue;
            }
            const PendingInteraction pending = found->second;
            _pending.erase(found);

            // Still the same container, still where the press was made: a
            // container that streamed out and back is a new entity.
            Stash *stash = FindStashReplica(pending.stashNetworkId);
            if (verdict.allowed && stash != nullptr && stash->GetNativeEntityId() == pending.entityId) {
                Replay(pending);
            }
            else if (!verdict.allowed) {
                Framework::Logging::GetLogger(kLogger)->debug("Server refused {} on stash {}", Shared::RPC::StashActionName(pending.action), pending.stashNetworkId);
                const Stash *contents = ContentsReplica(stash);
                KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction(contents != nullptr && contents->isOpen ? "Someone else is using this container." : "You cannot use this container right now.");
            }
            ReleasePending(pending);
        }
    }

    void StashService::Replay(const PendingInteraction &pending) {
        // Recorded first: a lock easy enough for the player's skill is picked
        // inside `C_LockPicking::Start`, and its success -- and the open the
        // stash's Lua does on it -- happen before the replay returns.
        if (pending.action == Shared::RPC::StashAction::Lockpick) {
            _lockpickGrants.insert_or_assign(pending.entityId, static_cast<std::uint64_t>(MafiaNet::GetTime() + kLockpickGrantTimeoutMs));
        }

        ++_replayDepth;
        _replayEntityId = pending.entityId;
        _replayAction   = pending.action;

        // Through the hooked entry, which lets it pass while a replay is on:
        // the handler runs exactly as the game would have run it.
        SDK::wh::entitymodule::InvokeLuaAction(pending.function, pending.entityId);

        --_replayDepth;
        _replayEntityId = 0;
    }

    void StashService::ReleasePending(const PendingInteraction &pending) {
        if (pending.scriptSystem != nullptr && pending.function != 0) {
            pending.scriptSystem->ReleaseFunc(pending.function);
        }
    }

    void StashService::ExpirePending() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        for (auto it = _pending.begin(); it != _pending.end();) {
            if (it->second.expiresAt > now) {
                ++it;
                continue;
            }
            Framework::Logging::GetLogger(kLogger)->warn("Server never answered {} on stash {}; dropping it", Shared::RPC::StashActionName(it->second.action), it->second.stashNetworkId);
            ReleasePending(it->second);
            it = _pending.erase(it);
        }
        std::erase_if(_lockpickGrants, [now](const auto &grant) {
            return grant.second <= now;
        });
    }

    void StashService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        const bool contextChanged = _nativeContextChanged.exchange(false);
        if (contextChanged) {
            for (const auto &[networkId, pending] : _virtualOpens) {
                SendVirtualClosed(networkId);
            }
            _virtualOpens.clear();
            replication->ForEach<Stash>([](auto *stash) {
                stash->RetireView();
            });
        }
        std::vector<IncomingMessage> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
        }
        for (const auto &message : incoming) {
            try {
                const auto &snapshot = message.snapshot;
                const auto networkId = message.stashNetworkId;
                if (message.kind == IncomingMessageKind::Close) {
                    _virtualOpens.erase(networkId);
                    if (auto *stash = ContentsReplica(replication->GetEntity<Stash>(networkId))) {
                        stash->ServerClosed();
                    }
                    continue;
                }
                if (message.kind == IncomingMessageKind::VirtualOpen) {
                    if (!contextChanged && (_virtualOpens.contains(networkId) || _virtualOpens.size() < 64)) {
                        _virtualOpens.insert_or_assign(networkId, PendingVirtualOpen {snapshot, static_cast<std::uint64_t>(MafiaNet::GetTime() + kPendingTimeoutMs)});
                    }
                    else {
                        SendVirtualClosed(networkId);
                    }
                    continue;
                }
                if (auto pending = _virtualOpens.find(networkId); pending != _virtualOpens.end()) {
                    pending->second.snapshot = snapshot;
                }
                // Addressed to the chest that was opened; a linked child's rows are its master's.
                if (auto *stash = ContentsReplica(replication->GetEntity<Stash>(networkId))) {
                    stash->OnServerContents(snapshot);
                }
            }
            catch (const nlohmann::json::exception &) {
            }
        }

        for (auto pending = _virtualOpens.begin(); pending != _virtualOpens.end();) {
            auto *stash = replication->GetEntity<Stash>(pending->first);
            if (stash != nullptr && stash->isVirtual) {
                stash->OnVirtualOpen(pending->second.snapshot);
                pending = _virtualOpens.erase(pending);
            }
            else if (pending->second.expiresAt <= MafiaNet::GetTime()) {
                SendVirtualClosed(pending->first);
                pending = _virtualOpens.erase(pending);
            }
            else {
                ++pending;
            }
        }

        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> changed;
        {
            std::scoped_lock lock(_projectedMutex);
            changed.swap(_changedHolders);
        }
        replication->ForEach<Stash>([&changed](Stash *stash) {
            const auto *projected = stash->ProjectedInventory();
            if (projected != nullptr && std::find(changed.begin(), changed.end(), projected) != changed.end()) {
                stash->NotifyNativeChange();
            }
            stash->Update();
        });
        RefreshIndex();
        ApplyVerdicts();
        ExpirePending();
    }

    void StashService::Reset() {
        for (const auto &[requestId, pending] : _pending) {
            ReleasePending(pending);
        }
        _pending.clear();
        {
            std::scoped_lock lock(_incomingMutex);
            _incomingVerdicts.clear();
        }
        _bound.clear();
        _actionTypes.clear();
        _lockpickGrants.clear();
        _replayDepth          = 0;
        _replayEntityId       = 0;
        _nextSpawnOrdinal     = 1;
        _nativeContextChanged = false;
        {
            std::scoped_lock lock(_projectedMutex);
            _projected.clear();
            _changedHolders.clear();
        }
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
        _virtualOpens.clear();
    }

    void StashService::NativeContextChanged() {
        _nativeContextChanged = true;
    }
} // namespace KCDC::Features::Stash
