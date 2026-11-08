#include "npc_inventory_module.h"

#include "core/application.h"
#include "core/obfuscation.h"
#include "common/module_support.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/framework/i_ui_item_transfer.h"
#include "shared/game/item_classes.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace KCDC::Features::Npc {
    NpcInventoryModule &NpcInventoryModule::Get() {
        static NpcInventoryModule instance;
        return instance;
    }

    void NpcInventoryModule::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::NpcInventoryReply>([](const auto &reply, MafiaNet::Packet *) {
            if (reply.valid) {
                Get().QueueReply(reply);
            }
        });
    }

    Npc *NpcInventoryModule::Find(std::uint64_t id) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr ? replication->GetEntity<Npc>(id) : nullptr;
    }

    Npc *NpcInventoryModule::FindBody(SDK::EntityId entity) {
        Npc *found = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication(); replication != nullptr && entity != 0) {
            replication->ForEach<Npc>([&](Npc *npc) {
                if (npc->GetEntityId() == entity) {
                    found = npc;
                }
            });
        }
        return found;
    }

    bool NpcInventoryModule::CanUse(const Npc &npc) {
        auto *local = KCDC::Common::LocalHumanActor();
        auto *actor = npc.ResolveActor();
        auto *soul  = local != nullptr ? local->GetSoul() : nullptr;
        if (npc.alive || !KCDC::Shared::Game::HasFlag(npc.flags, KCDC::Shared::Game::NpcFlags::Lootable) || local == nullptr || actor == nullptr || soul == nullptr || soul->IsDead() || local->GetEntity() == nullptr || actor->GetEntity() == nullptr) {
            return false;
        }
        const auto player = local->GetEntity()->GetWorldPos();
        const auto target = actor->GetEntity()->GetWorldPos();
        return glm::distance(glm::vec3(player.x, player.y, player.z), glm::vec3(target.x, target.y, target.z)) <= KCDC::Shared::NpcInventory::kUseDistance;
    }

    bool NpcInventoryModule::Busy(Npc &npc) const {
        const auto ownToken = _session.npc == npc.GetNetworkID() && _session.epoch == npc.spawnEpoch ? _session.token : 0;
        return npc.InventoryBusyFor(ownToken);
    }

    std::uint32_t NpcInventoryModule::Send(KCDC::Shared::NpcInventory::Action action, std::uint64_t npc, std::uint32_t epoch, std::uint64_t token, nlohmann::json fields) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return 0;
        }
        const auto request = _nextRequest++;
        if (_nextRequest == 0) {
            _nextRequest = 1;
        }
        fields["npc"]     = npc;
        fields["epoch"]   = epoch;
        fields["request"] = request;
        fields["action"]  = static_cast<std::uint8_t>(action);
        fields["token"]   = token;
        KCDC::Shared::RPC::NpcInventoryRequest message;
        message.body = fields.dump();
        if (message.body.size() > KCDC::Shared::RPC::InventoryMessage::kMaximumBytes) {
            return 0;
        }
        net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return request;
    }

    void NpcInventoryModule::Seed(Npc &npc) {
        const auto now = MafiaNet::GetTimeUS() / 1000;
        if (const auto found = _seeds.find(npc.GetNetworkID()); found != _seeds.end() && found->second.epoch == npc.spawnEpoch && now < found->second.deadline) {
            return;
        }
        nlohmann::json rows = nlohmann::json::array();
        if (!npc.StockKnown() && !npc.ReadInitialInventory(rows)) {
            return;
        }
        const auto *actor      = npc.ResolveActor();
        const auto *soul       = actor != nullptr ? actor->GetSoul() : nullptr;
        const auto *nativeName = soul != nullptr ? soul->GetName() : nullptr;
        if (nativeName == nullptr) {
            return;
        }
        const auto request = Send(KCDC::Shared::NpcInventory::Action::Seed, npc.GetNetworkID(), npc.spawnEpoch, 0, {{"rows", rows}, {"nativeSoul", nativeName}});
        _seeds.insert_or_assign(npc.GetNetworkID(), SeedRequest {request, npc.spawnEpoch, now + 5000});
    }

    void NpcInventoryModule::Begin(Npc &npc, KCDC::Shared::NpcInventory::Action action) {
        End();
        _session.npc      = npc.GetNetworkID();
        _session.entity   = npc.GetEntityId();
        _session.epoch    = npc.spawnEpoch;
        _session.action   = action;
        _session.stage    = Stage::Requesting;
        _session.deadline = MafiaNet::GetTimeUS() / 1000 + 10000;
        _session.request  = Send(action, _session.npc, _session.epoch, 0);
    }

    bool NpcInventoryModule::RequestExchange(SDK::wh::entitymodule::C_Actor &actor, SDK::EntityId user) {
        if (_replaying) {
            return false;
        }
        Npc *npc = FindBody(actor.GetEntityId());
        if (npc == nullptr) {
            return false;
        }
        auto *local = KCDC::Common::LocalHumanActor();
        if (local == nullptr || user != local->GetEntityId() || !CanUse(*npc) || Busy(*npc)) {
            return true;
        }
        if (_session.npc == npc->GetNetworkID() && _session.epoch == npc->spawnEpoch && _session.stage != Stage::Idle) {
            _session.wantOpen = true;
            return true;
        }
        if (!npc->InventoryReady() || npc->CanBeHarvested()) {
            return true;
        }
        Begin(*npc, KCDC::Shared::NpcInventory::Action::Open);
        _session.wantOpen = true;
        return true;
    }

    void NpcInventoryModule::RegisterAction(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function) {
        auto *npc = FindBody(interactor.GetTargetEntityId());
        if (npc == nullptr || function == 0) {
            return;
        }
        const char *action = record.m_action.m_pData;
        if (action == nullptr) {
            return;
        }
        if (std::strcmp(action, "butcher") == 0) {
            _butcherFunctions.insert_or_assign(interactor.GetTargetEntityId(), function);
            if (!CanUse(*npc) || !npc->InventoryReady() || !npc->CanBeHarvested()) {
                record.m_enabled   = false;
                record.m_uiVisible = false;
            }
            else if (Busy(*npc)) {
                record.m_enabled = false;
            }
        }
        else if (std::strcmp(action, "use") == 0 && !npc->alive && (!CanUse(*npc) || !npc->InventoryReady() || npc->CanBeHarvested())) {
            record.m_enabled   = false;
            record.m_uiVisible = false;
        }
        else if (std::strcmp(action, "use") == 0 && !npc->alive && Busy(*npc)) {
            record.m_enabled = false;
        }
    }

    bool NpcInventoryModule::InterceptAction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation) {
        const auto callback = _butcherFunctions.find(invocation.m_targetEntityId);
        if (_replaying || callback == _butcherFunctions.end() || callback->second != invocation.m_function) {
            return false;
        }
        auto *npc = FindBody(invocation.m_targetEntityId);
        if (npc == nullptr) {
            return false;
        }
        if (!CanUse(*npc) || !npc->InventoryReady() || !npc->CanBeHarvested() || Busy(*npc) || _session.stage != Stage::Idle) {
            return true;
        }
        auto *scripts = SDK::IScriptSystem::GetInstance();
        if (scripts == nullptr) {
            return true;
        }
        const auto function = scripts->AddFuncRef(invocation.m_function);
        if (function == 0) {
            return true;
        }
        Begin(*npc, KCDC::Shared::NpcInventory::Action::Harvest);
        _session.scripts  = scripts;
        _session.function = function;
        return true;
    }

    bool NpcInventoryModule::StartButchering(SDK::wh::entitymodule::C_ActorActionButchering &action) {
        auto *npc = FindBody(action.GetTargetEntityId());
        if (npc == nullptr) {
            return true;
        }
        auto *local = KCDC::Common::LocalHumanActor();
        if (_session.stage != Stage::Harvesting || _session.npc != npc->GetNetworkID() || _session.epoch != npc->spawnEpoch || _session.token == 0 || !CanUse(*npc) || local == nullptr || action.GetActionActor() != local->GetActionActor()) {
            return false;
        }
        _session.nativeAction = &action;
        return true;
    }

    void NpcInventoryModule::EnteredButchering(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool accepted) {
        if (_session.nativeAction != &action) {
            return;
        }
        if (!accepted) {
            _session.cancelled    = !_session.completed;
            _session.nativeAction = nullptr;
            return;
        }
        auto *local     = KCDC::Common::LocalHumanActor();
        auto *inventory = local != nullptr ? local->GetInventoryRuntime() : nullptr;
        if (inventory == nullptr) {
            return;
        }
        const auto toolClass = KCDC::Shared::Game::ItemClassIdFromGuid(KCDC::Shared::Game::kButcheringToolClassGuid);
        for (std::size_t index = 0; index < inventory->GetItemCount(); ++index) {
            auto *item = inventory->GetItemAt(index);
            if (item == nullptr || item->GetCreationReason() != 13) {
                continue;
            }
            const auto itemClass = item->InvokeGetItemClassRaw();
            if (itemClass.m_low != toolClass.low || itemClass.m_high != toolClass.high) {
                continue;
            }
            // Match the native Exit predicate, including the creation reason:
            // an owned knife of the same class must remain authoritative.
            _session.toolWuid = item->GetId();
            KCDC::Core::gApplication->GetPlayerInventoryService().LendNativeItem(_session.toolWuid);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Preserved temporary harvest tool {}", _session.toolWuid);
            return;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The native harvest action did not create its temporary tool");
    }

    bool NpcInventoryModule::CompletedButchering(SDK::wh::entitymodule::C_ActorActionButchering &action) {
        auto *npc = FindBody(action.GetTargetEntityId());
        if (npc == nullptr && _session.nativeAction != &action) {
            return false;
        }
        // The native completion would mint loot and XP. Managed bodies use
        // the gamemode's existing stock; only the accepted server completion
        // changes their appearance and unlocks that stock.
        action.MarkCompleted();
        if (npc == nullptr || _session.nativeAction != &action || _session.stage != Stage::Harvesting || _session.npc != npc->GetNetworkID() || _session.epoch != npc->spawnEpoch || _session.token == 0 || !CanUse(*npc)) {
            return true;
        }
        _session.completed = true;
        _session.stage     = Stage::Completing;
        _session.action    = KCDC::Shared::NpcInventory::Action::Complete;
        // Send after the native completion/exit stack unwinds.
        _session.request  = 0;
        _session.deadline = MafiaNet::GetTimeUS() / 1000 + 10000;
        return true;
    }

    void NpcInventoryModule::ExitedButchering(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool cancelled) {
        if (_session.nativeAction != &action) {
            return;
        }
        _session.nativeAction = nullptr;
        if (_session.toolWuid != 0) {
            KCDC::Core::gApplication->GetPlayerInventoryService().ReturnNativeItem(_session.toolWuid);
            _session.toolWuid = 0;
        }
        // The exit callback only records cancellation. Closing the native UI
        // and releasing the grant run after its action stack has unwound.
        _session.cancelled = cancelled || !_session.completed;
    }

    bool NpcInventoryModule::ManagesHolder(const SDK::wh::entitymodule::C_ItemHolder *holder) const {
        const auto &inventories = _holders.Current().inventories;
        return holder != nullptr && std::find(inventories.begin(), inventories.end(), holder) != inventories.end();
    }

    bool NpcInventoryModule::ManagesItem(const SDK::wh::entitymodule::C_Item *item) const {
        return item != nullptr && ManagesHolder(item->GetOwner());
    }

    // Not filtered by `ManagesHolder`: a body's inventory is listed from the
    // next `Update` on, and the game strips armor the spawn tick equipped
    // (`ReconcileArmorToOutfit`, 0x1804763D8) before then. `Update` matches
    // the queue against every NPC's inventory itself.
    void NpcInventoryModule::NotifyNativeChange(const SDK::wh::entitymodule::C_ItemHolder *holder) {
        if (holder == nullptr || KCDC::Core::gApplication->GetPlayerInventoryService().IsApplyingAuthority()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (std::find(_changedHolders.begin(), _changedHolders.end(), holder) == _changedHolders.end()) {
            _changedHolders.push_back(holder);
        }
    }

    bool NpcInventoryModule::AnswerCanBeButchered(const SDK::wh::rpgmodule::I_Soul *soul, bool &answer) const {
        auto *actor = soul != nullptr ? soul->GetActor(0) : nullptr;
        auto *npc   = actor != nullptr ? FindBody(actor->GetEntityId()) : nullptr;
        if (npc == nullptr) {
            return false;
        }
        answer = !npc->alive && npc->CanBeHarvested() && KCDC::Shared::Game::HasFlag(npc->flags, KCDC::Shared::Game::NpcFlags::Lootable);
        return true;
    }

    bool NpcInventoryModule::RequestTransfer(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount) {
        auto &player = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (!player.HasAuthority() || player.IsApplyingAuthority()) {
            return false;
        }
        auto *npc           = Find(_session.npc);
        auto *actor         = KCDC::Common::LocalHumanActor();
        auto *local         = actor != nullptr ? actor->GetInventoryRuntime() : nullptr;
        auto *native        = npc != nullptr ? npc->ProjectedInventory() : nullptr;
        auto *source        = !items.IsEmpty() && items[0] != nullptr ? items[0]->GetOwner() : nullptr;
        const bool deposit  = native != nullptr && destination == native && source == local;
        const bool withdraw = native != nullptr && destination == local && source == native;
        if (!deposit && !withdraw) {
            return false;
        }
        if (_session.stage != Stage::Open || _session.token == 0 || amount == 0 || !CanUse(*npc) || !npc->InventoryReady() || !player.CanSpend() || _session.transferPending || (!_session.units.empty() && _session.deposit != deposit)) {
            return true;
        }
        auto selected = _session.units;
        std::unordered_set<std::uint64_t> seen;
        for (auto *item : items) {
            if (amount == 0) {
                break;
            }
            if (item == nullptr || item->GetOwner() != source || item->GetAmount() == 0 || !seen.insert(item->GetId()).second) {
                return true;
            }
            auto remaining = std::min(amount, item->GetAmount());
            amount -= remaining;
            const auto units = deposit ? player.UnitsOf(item->GetId()) : npc->InventoryProjection().UnitsOf(item->GetId());
            for (const auto &unit : units) {
                if (selected[unit.id] > unit.amount) {
                    return true;
                }
                const auto take = std::min(remaining, unit.amount - selected[unit.id]);
                selected[unit.id] += take;
                remaining -= take;
                if (remaining == 0) {
                    break;
                }
            }
            if (remaining != 0 || selected.size() > KCDC::Shared::NpcInventory::kMaximumRows) {
                return true;
            }
        }
        _session.units   = std::move(selected);
        _session.deposit = deposit;
        return true;
    }

    void NpcInventoryModule::FlushTransfer(Npc &npc) {
        auto &player = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (_session.units.empty() || _session.transferPending || !npc.InventoryReady() || !player.CanSpend() || player.SyncedRevision() < _session.playerRevision) {
            return;
        }
        nlohmann::json units = nlohmann::json::array();
        for (const auto &[id, amount] : _session.units) {
            if (amount != 0) {
                units.push_back({{"id", id}, {"amount", amount}});
            }
        }
        _session.request = Send(KCDC::Shared::NpcInventory::Action::Transfer, _session.npc, _session.epoch, _session.token, {{"units", units}, {"deposit", _session.deposit}, {"npcRevision", npc.InventoryRevision()}, {"playerRevision", player.SyncedRevision()}});
        _session.units.clear();
        _session.transferPending = true;
        _session.deadline        = MafiaNet::GetTimeUS() / 1000 + 10000;
    }

    void NpcInventoryModule::QueueReply(const KCDC::Shared::RPC::NpcInventoryReply &reply) {
        auto body = nlohmann::json::parse(reply.body, nullptr, false);
        if (!body.is_object()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incoming.size() < 128) {
            _incoming.push_back(std::move(body));
        }
    }

    void NpcInventoryModule::ApplyReply(const nlohmann::json &reply) {
        std::uint64_t id = 0, token = 0;
        std::uint32_t epoch = 0, request = 0;
        KCDC::Shared::NpcInventory::Action action;
        if (!KCDC::Shared::NpcInventory::ReadRequest(reply, id, epoch, request, action, token) || !reply.contains("ok") || !reply.at("ok").is_boolean()) {
            return;
        }
        auto *npc = Find(id);
        if (action == KCDC::Shared::NpcInventory::Action::Seed) {
            const auto seed = _seeds.find(id);
            if (seed != _seeds.end() && seed->second.request == request && seed->second.epoch == epoch && npc != nullptr && npc->spawnEpoch == epoch && reply.value("ok", false) && reply.contains("stock")) {
                npc->AcceptInventory(reply.at("stock"));
                _seeds.erase(seed);
            }
            return;
        }
        const bool renewal = action == KCDC::Shared::NpcInventory::Action::Renew && _session.renewal == request;
        if (_session.npc != id || _session.epoch != epoch || (!renewal && _session.request != request) || npc == nullptr || npc->spawnEpoch != epoch || npc->GetEntityId() != _session.entity) {
            if (reply.value("ok", false) && token != 0 && (action == KCDC::Shared::NpcInventory::Action::Open || action == KCDC::Shared::NpcInventory::Action::Harvest)) {
                Send(KCDC::Shared::NpcInventory::Action::Close, id, epoch, token);
            }
            return;
        }
        if (renewal) {
            _session.renewal = 0;
        }
        if (reply.contains("stock")) {
            npc->AcceptInventory(reply.at("stock"));
        }
        if (!reply.value("ok", false)) {
            KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction("The body is busy or its inventory has changed. Please try again.");
            End();
            return;
        }
        _session.token   = token;
        _session.renewAt = MafiaNet::GetTimeUS() / 1000 + 30000;
        if (action == KCDC::Shared::NpcInventory::Action::Transfer) {
            _session.transferPending = false;
            if (reply.contains("playerRevision") && reply.at("playerRevision").is_number_unsigned()) {
                _session.playerRevision = reply.at("playerRevision").get<std::uint64_t>();
            }
        }
        else if (action == KCDC::Shared::NpcInventory::Action::Open || action == KCDC::Shared::NpcInventory::Action::Harvest || action == KCDC::Shared::NpcInventory::Action::Complete) {
            _session.stage    = Stage::Projecting;
            _session.deadline = MafiaNet::GetTimeUS() / 1000 + 10000;
        }
    }

    void NpcInventoryModule::ReleaseFunction() {
        if (_session.scripts != nullptr && _session.function != 0) {
            _session.scripts->ReleaseFunc(_session.function);
        }
        _session.scripts  = nullptr;
        _session.function = 0;
    }

    void NpcInventoryModule::ReleaseBody(Npc &npc) {
        // Close while the inventory and its owning actor are still alive.
        if (_session.npc == npc.GetNetworkID()) {
            End();
        }
        _seeds.erase(npc.GetNetworkID());
        _butcherFunctions.erase(npc.GetEntityId());
    }

    void NpcInventoryModule::End(bool closeScreen) {
        if (_session.token != 0) {
            Send(KCDC::Shared::NpcInventory::Action::Close, _session.npc, _session.epoch, _session.token);
        }
        if (closeScreen) {
            // Detach before Cancel: the queue may run Exit synchronously.
            // End(false) never touches an action from an unloaded world.
            auto *action          = _session.nativeAction;
            _session.nativeAction = nullptr;
            if (action != nullptr) {
                action->Cancel();
            }
            auto *npc         = Find(_session.npc);
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            auto *screen      = environment != nullptr ? environment->GetUIItemTransfer() : nullptr;
            auto *inventory   = npc != nullptr ? npc->ProjectedInventory() : nullptr;
            if (screen != nullptr && inventory != nullptr && screen->IsOpen() && screen->IsUsingInventory(inventory)) {
                screen->Close(false);
            }
        }
        ReleaseFunction();
        if (_session.toolWuid != 0 && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetPlayerInventoryService().ReturnNativeItem(_session.toolWuid);
            KCDC::Core::gApplication->GetPlayerInventoryService().NotifyNativeChange();
        }
        _session = {};
    }

    void NpcInventoryModule::Update() {
        if (_contextChanged.exchange(false)) {
            Reset();
        }
        std::vector<nlohmann::json> incoming;
        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> changed;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
            changed.swap(_changedHolders);
        }
        for (const auto &reply : incoming) {
            ApplyReply(reply);
        }
        auto &holders = _holders.Next().inventories;
        holders.clear();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Npc>([&](Npc *npc) {
                if (auto *inventory = npc->ProjectedInventory()) {
                    if (std::find(changed.begin(), changed.end(), inventory) != changed.end()) {
                        npc->NotifyNativeInventoryChange();
                    }
                    holders.push_back(inventory);
                }
            });
        }
        _holders.Publish();
        if (_session.stage == Stage::Idle) {
            return;
        }
        auto *npc      = Find(_session.npc);
        const auto now = MafiaNet::GetTimeUS() / 1000;
        if (_session.cancelled || npc == nullptr || npc->spawnEpoch != _session.epoch || npc->GetEntityId() != _session.entity || !CanUse(*npc) || (_session.token != 0 && npc->InventoryToken() != _session.token)
            || (now >= _session.deadline && (_session.stage != Stage::Open || _session.transferPending))) {
            End();
            return;
        }
        if (_session.stage == Stage::Completing && _session.request == 0) {
            _session.request = Send(KCDC::Shared::NpcInventory::Action::Complete, _session.npc, _session.epoch, _session.token);
        }
        if (_session.stage == Stage::Projecting && (_session.action != KCDC::Shared::NpcInventory::Action::Complete || _session.nativeAction == nullptr) && npc->ApplyInventory() && npc->InventoryReady()) {
            if (_session.action == KCDC::Shared::NpcInventory::Action::Harvest && !_session.completed && _session.function != 0) {
                _session.stage    = Stage::Harvesting;
                _session.deadline = now + 10000;
                _replaying        = true;
                SDK::wh::entitymodule::InvokeLuaAction(_session.function, _session.entity);
                _replaying = false;
                ReleaseFunction();
            }
            else {
                _session.stage    = Stage::Open;
                _session.deadline = now + 10000;
            }
        }
        if (_session.stage == Stage::Harvesting && _session.nativeAction != nullptr) {
            _session.deadline = now + 10000;
        }
        if (_session.token != 0 && now >= _session.renewAt && _session.renewal == 0 && !_session.transferPending && _session.stage != Stage::Completing) {
            _session.renewAt = now + 30000;
            _session.renewal = Send(KCDC::Shared::NpcInventory::Action::Renew, _session.npc, _session.epoch, _session.token);
        }
        if (_session.stage == Stage::Open) {
            if (_session.wantOpen && !_session.opening && npc->InventoryReady()) {
                _session.opening  = true;
                _session.deadline = now + 10000;
                _replaying        = true;
                npc->ResolveActor()->RequestItemExchange(KCDC::Common::LocalHumanActor()->GetEntityId());
                _replaying = false;
            }
            FlushTransfer(*npc);
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            auto *screen      = environment != nullptr ? environment->GetUIItemTransfer() : nullptr;
            const bool opened = screen != nullptr && screen->IsOpen() && screen->IsUsingInventory(npc->ProjectedInventory());
            if (opened) {
                _session.screenOpened = true;
            }
            else if (!_session.transferPending && _session.units.empty() && (_session.screenOpened || now >= _session.deadline)) {
                End(false);
            }
        }
    }

    void NpcInventoryModule::Reset() {
        End(false);
        _seeds.clear();
        _butcherFunctions.clear();
        _holders.Reset([](Holders &slot) {
            slot.inventories.clear();
        });
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
        _changedHolders.clear();
    }
} // namespace KCDC::Features::Npc
