#include "npc_inventory_module.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/inventory/inventory_gameplay.h"
#include "features/inventory/inventory_scripting.h"
#include "features/inventory/inventory_service.h"
#include "features/npc/npc_verbs.h"
#include "features/player/player_roster.h"
#include "shared/game/animal_loot.h"
#include "shared/game/item_classes.h"

#include <core_modules.h>
#include <mafianet/get_time.h>
#include <mafianet/virtual_world.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>

namespace KCDC::Features::Npc {
    NpcInventoryModule &NpcInventoryModule::Get() {
        static NpcInventoryModule instance;
        return instance;
    }

    void NpcInventoryModule::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::NpcInventoryRequest>([](const auto &message, MafiaNet::Packet *packet) {
            if (packet != nullptr && message.valid) {
                Get().Receive(MafiaNet::ToPeerGuid(packet->guid), message);
            }
        });
    }

    void NpcInventoryModule::Receive(MafiaNet::PeerGuid peer, const KCDC::Shared::RPC::NpcInventoryRequest &message) {
        if (message.body.size() > KCDC::Shared::RPC::InventoryMessage::kMaximumBytes) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_incoming.size() < 128 && std::count_if(_incoming.begin(), _incoming.end(), [peer](const auto &entry) {
                return entry.peer == peer;
            }) < 8) {
            _incoming.push_back({peer, message.body});
        }
    }

    void NpcInventoryModule::Create(KCDC::Shared::Entities::NpcEntity &npc) {
        auto record         = std::make_unique<Record>(Server::Get()->GetInventory().Ids());
        const auto *soul    = KCDC::Shared::Game::FindAnimalSoul(npc.soulGuid);
        record->animal      = soul != nullptr && !soul->lootable ? nullptr : KCDC::Shared::Game::FindAnimalLoot(soul != nullptr ? soul->actorClass : npc.SpawnClass());
        record->nativeKnown = npc.levelGuid == 0;
        if (record->animal != nullptr) {
            record->inventory.Seed({}, {});
        }
        // An authored outfit has a complete baseline already. Native default
        // outfits are censused once by the elected simulator, after dressing.
        if (record->animal == nullptr && !npc.equipment.empty()) {
            std::vector<KCDC::Features::Inventory::InventoryRow> rows;
            for (const auto &item : Npcs::GetWearing(&npc)) {
                rows.push_back({{}, item, 1, nlohmann::json::object()});
            }
            record->inventory.Seed(rows, std::vector<std::uint32_t>(rows.size(), 1));
        }
        Publish(npc, *record);
        _records.emplace(npc.GetNetworkID(), std::move(record));
    }

    NpcInventory *NpcInventoryModule::Find(std::uint64_t npc) {
        const auto found = _records.find(npc);
        if (found != _records.end() && Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::NpcEntity>(npc) == nullptr) {
            _records.erase(found);
            return nullptr;
        }
        return found != _records.end() ? &found->second->inventory : nullptr;
    }

    void NpcInventoryModule::Destroy(std::uint64_t npc) {
        _records.erase(npc);
    }

    void NpcInventoryModule::Forget(std::uint64_t player) {
        for (auto &[id, record] : _records) {
            if (record->inventory.Holder() == player) {
                record->inventory.Release();
            }
        }
    }

    void NpcInventoryModule::Revived(KCDC::Shared::Entities::NpcEntity &npc) {
        if (const auto found = _records.find(npc.GetNetworkID()); found != _records.end()) {
            found->second->inventory.Revived();
            Publish(npc, *found->second);
        }
    }

    bool NpcInventoryModule::Dress(KCDC::Shared::Entities::NpcEntity &npc, const std::vector<std::string> &classes) {
        auto *stock = Find(npc.GetNetworkID());
        if (stock == nullptr) {
            return false;
        }
        if (!stock->Ready()) {
            std::vector<KCDC::Features::Inventory::InventoryRow> rows;
            for (const auto &item : classes) {
                rows.push_back({{}, item, 1, nlohmann::json::object()});
            }
            if (!stock->Seed(rows, std::vector<std::uint32_t>(rows.size(), 1))) {
                return false;
            }
        }
        else if (!stock->Dress(classes)) {
            return false;
        }
        Changed(npc.GetNetworkID());
        return true;
    }

    void NpcInventoryModule::Publish(KCDC::Shared::Entities::NpcEntity &npc, Record &record) {
        record.inventory.ClampEquipment();
        if (record.stateRevision != 0 && record.publishedInventoryRevision == record.inventory.Stock().Revision() && record.publishedEquipment == record.inventory.Equipped() && record.publishedReady == record.inventory.Ready() && record.publishedNativeKnown == record.nativeKnown
            && record.publishedInteraction == record.inventory.Interaction()) {
            return;
        }
        record.publishedInteraction       = record.inventory.Interaction();
        record.publishedInventoryRevision = record.inventory.Stock().Revision();
        record.publishedEquipment         = record.inventory.Equipped();
        record.publishedReady             = record.inventory.Ready();
        record.publishedNativeKnown       = record.nativeKnown;
        auto state                        = record.inventory.Snapshot();
        state["nativeKnown"]              = record.nativeKnown;
        state["harvestable"]              = record.animal != nullptr && !record.animal->items.empty();
        state["state"]                    = ++record.stateRevision;
        npc.inventoryState                = state.dump();
        if (record.inventory.Ready()) {
            npc.equipment.clear();
            for (const auto &[id, count] : record.inventory.Equipped()) {
                KCDC::Shared::Game::ItemClassIdFromGuid(record.inventory.Stock().Rows().at(id).itemClass).AppendPackedText(npc.equipment);
            }
        }
        npc.ForceState();
    }

    void NpcInventoryModule::Changed(std::uint64_t npc) {
        const auto found = _records.find(npc);
        auto *entity     = Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::NpcEntity>(npc);
        if (found != _records.end() && entity != nullptr) {
            Publish(*entity, *found->second);
        }
    }

    nlohmann::json NpcInventoryModule::AnimalLoot(std::uint64_t npc) const {
        const auto found = _records.find(npc);
        if (found == _records.end() || found->second->animal == nullptr || Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::NpcEntity>(npc) == nullptr) {
            return nullptr;
        }
        nlohmann::json items = nlohmann::json::array();
        for (const auto &item : found->second->animal->items) {
            items.push_back({{"item", item.item}, {"amount", item.amount}, {"fraction", item.fraction}, {"variation", item.variation}, {"food", item.food}, {"harvester", item.harvester}});
        }
        return items;
    }

    nlohmann::json NpcInventoryModule::ScriptGet(std::uint64_t npc) {
        const auto *inventory = Find(npc);
        if (inventory == nullptr) {
            return nullptr;
        }
        auto state         = KCDC::Features::Inventory::InventoryService::EncodeInventory(inventory->Stock());
        state["ready"]     = inventory->Ready();
        state["harvested"] = inventory->Harvested();
        for (auto &row : state["items"]) {
            row["equipped"] = inventory->Equipped().contains(row["id"].get<std::string>()) ? inventory->Equipped().at(row["id"].get<std::string>()) : 0U;
        }
        return state;
    }

    nlohmann::json NpcInventoryModule::ScriptSet(std::uint64_t npc, const nlohmann::json &request) {
        auto *inventory = Find(npc);
        if (inventory == nullptr) {
            return {{"ok", false}, {"code", "inventoryUnavailable"}};
        }
        std::vector<std::uint32_t> equipped;
        std::size_t equippedRows = 0;
        if (!request.is_object() || !request.contains("items") || !request.at("items").is_array()) {
            return {{"ok", false}, {"code", "invalidRequest"}};
        }
        if (request.contains("harvested") && !request.at("harvested").is_boolean()) {
            return {{"ok", false}, {"code", "invalidRequest"}};
        }
        for (const auto &row : request.at("items")) {
            std::uint64_t count = 0, amount = 0;
            if (!row.is_object() || !KCDC::Shared::NpcInventory::Unsigned(row, "amount", amount, KCDC::Shared::NpcInventory::kMaximumAmount)
                || (row.contains("equipped")
                    && (!KCDC::Shared::NpcInventory::Unsigned(row, "equipped", count, amount) || (count != 0 && (!row.contains("item") || !row.at("item").is_string() || !KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(row.at("item").get<std::string>())))))) {
                return {{"ok", false}, {"code", "invalidEquipment"}};
            }
            equipped.push_back(static_cast<std::uint32_t>(count));
            equippedRows += count != 0 ? 1 : 0;
        }
        if (equippedRows > KCDC::Features::Inventory::InventoryGameplay::kMaximumEquippedRows) {
            return {{"ok", false}, {"code", "invalidEquipment"}};
        }
        auto result = KCDC::Features::Inventory::InventoryService::ScriptSet(&inventory->Stock(), request);
        if (result.value("ok", false)) {
            std::map<std::string, std::uint32_t> worn;
            for (std::size_t i = 0; i < equipped.size(); ++i) {
                if (equipped[i] != 0) {
                    worn.emplace(result.at("items").at(i).at("id").get<std::string>(), equipped[i]);
                }
            }
            inventory->Restored(std::move(worn), request.contains("harvested") ? std::optional<bool>(request.at("harvested").get<bool>()) : std::nullopt);
            Changed(npc);
        }
        return result;
    }

    bool NpcInventoryModule::CanAccess(std::uint64_t player, const KCDC::Shared::Entities::NpcEntity &npc) {
        const auto *human = KCDC::Features::Player::Players::Find(player);
        return human != nullptr
               && NpcInventory::CanAccess(Server::Get()->GetInventory().IsInitialized(player), human->soul.CanAct(), npc.alive, KCDC::Shared::Game::HasFlag(npc.flags, KCDC::Shared::Game::NpcFlags::Lootable),
                   MafiaNet::VirtualWorldsCanSee(human->GetVirtualWorld(), npc.GetVirtualWorld()), glm::distance(human->position, npc.position));
    }

    void NpcInventoryModule::Process(const Incoming &incoming) {
        const auto body  = nlohmann::json::parse(incoming.body, nullptr, false);
        std::uint64_t id = 0, token = 0;
        std::uint32_t epoch = 0, request = 0;
        KCDC::Shared::NpcInventory::Action action;
        if (!KCDC::Shared::NpcInventory::ReadRequest(body, id, epoch, request, action, token)) {
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *human       = replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(incoming.peer);
        if (human == nullptr) {
            return;
        }
        const auto player = human->GetNetworkID();
        auto *npc         = replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(id);
        const auto found  = _records.find(id);
        auto *stock       = found != _records.end() ? &found->second->inventory : nullptr;
        const auto now    = MafiaNet::GetTimeUS() / 1000;
        bool ok           = false;
        std::string code  = "unavailable";
        if (Server::Get()->GetInventory().Admit(player) && npc != nullptr && stock != nullptr && npc->spawnEpoch == epoch) {
            if (action == KCDC::Shared::NpcInventory::Action::Seed) {
                const bool simulator  = npc->ownerGUID == incoming.peer;
                const bool identified = found->second->nativeKnown || (body.contains("nativeSoul") && body.at("nativeSoul").is_string() && !body.at("nativeSoul").get_ref<const std::string &>().empty() && body.at("nativeSoul").get_ref<const std::string &>().size() <= 256);
                if (simulator && identified && !stock->Ready() && body.contains("rows") && body.at("rows").is_array() && body.at("rows").size() <= KCDC::Shared::NpcInventory::kMaximumRows) {
                    std::vector<KCDC::Features::Inventory::InventoryRow> rows;
                    std::vector<std::uint32_t> worn;
                    bool valid = true;
                    for (const auto &row : body.at("rows")) {
                        std::uint64_t amount = 0, equipped = 0;
                        if (!row.is_object() || !row.contains("item") || !row.at("item").is_string() || !row.contains("metadata") || !row.at("metadata").is_object() || !KCDC::Shared::NpcInventory::Unsigned(row, "amount", amount, KCDC::Shared::NpcInventory::kMaximumAmount)
                            || amount == 0 || !KCDC::Shared::NpcInventory::Unsigned(row, "equipped", equipped, amount)) {
                            valid = false;
                            break;
                        }
                        rows.push_back({{}, row.at("item").get<std::string>(), static_cast<std::uint32_t>(amount), row.at("metadata")});
                        worn.push_back(static_cast<std::uint32_t>(equipped));
                    }
                    ok = valid && stock->Seed(rows, worn);
                }
                else {
                    ok = simulator && identified && stock->Ready();
                }
                if (ok && !found->second->nativeKnown) {
                    const auto *animalSoul     = KCDC::Shared::Game::FindAnimalSoulByName(body.at("nativeSoul").get_ref<const std::string &>());
                    found->second->animal      = animalSoul != nullptr && animalSoul->lootable ? KCDC::Shared::Game::FindAnimalLoot(animalSoul->actorClass) : nullptr;
                    found->second->nativeKnown = true;
                }
            }
            else if (action == KCDC::Shared::NpcInventory::Action::Close || action == KCDC::Shared::NpcInventory::Action::Cancel) {
                ok = stock->Close(player, token);
            }
            else if (CanAccess(player, *npc)) {
                switch (action) {
                case KCDC::Shared::NpcInventory::Action::Open:
                case KCDC::Shared::NpcInventory::Action::Harvest:
                    if (action == KCDC::Shared::NpcInventory::Action::Open && stock->Holds(player, token, epoch, now)) {
                        ok = stock->Renew(player, token, epoch, now);
                    }
                    else {
                        // One player's screen/action owns at most one corpse.
                        Forget(player);
                        token = _nextToken++;
                        if (_nextToken == 0) {
                            _nextToken = 1;
                        }
                        ok = stock->Open(player, token, epoch, now, action == KCDC::Shared::NpcInventory::Action::Harvest, found->second->animal != nullptr && !found->second->animal->items.empty());
                    }
                    break;
                case KCDC::Shared::NpcInventory::Action::Renew: ok = stock->Renew(player, token, epoch, now); break;
                case KCDC::Shared::NpcInventory::Action::Complete: {
                    const bool wasHarvested = stock->Harvested();
                    ok                      = stock->Complete(player, token, epoch, now);
                    if (ok && !wasHarvested) {
                        _harvests.emplace_back(id, player);
                    }
                    break;
                }
                case KCDC::Shared::NpcInventory::Action::Transfer: {
                    std::uint64_t npcRevision = 0, playerRevision = 0;
                    const auto *units = body.contains("units") ? &body.at("units") : nullptr;
                    if (!stock->Holds(player, token, epoch, now) || !Server::Get()->GetInventory().IsReady(player) || !KCDC::Shared::NpcInventory::Unsigned(body, "npcRevision", npcRevision) || !KCDC::Shared::NpcInventory::Unsigned(body, "playerRevision", playerRevision)
                        || !body.contains("deposit") || !body.at("deposit").is_boolean() || units == nullptr || !KCDC::Shared::NpcInventory::ValidUnits(*units)) {
                        break;
                    }
                    std::vector<KCDC::Features::Inventory::InventoryUnit> selected;
                    for (const auto &unit : *units) {
                        selected.push_back({unit.at("id").get<std::string>(), unit.at("amount").get<std::uint32_t>()});
                    }
                    auto *inventory    = Server::Get()->GetInventory().Find(player);
                    const bool deposit = body.at("deposit").get<bool>();
                    const auto result  = stock->Transfer(*inventory, player, token, epoch, now, selected, deposit, npcRevision, playerRevision);
                    ok                 = result.ok;
                    code               = result.code;
                    break;
                }
                default: break;
                }
            }
            Publish(*npc, *found->second);
        }
        KCDC::Shared::RPC::NpcInventoryReply reply;
        nlohmann::json answer {{"npc", id}, {"epoch", epoch}, {"request", request}, {"action", static_cast<std::uint8_t>(action)}, {"token", ok ? token : 0U}, {"ok", ok}, {"code", ok ? "" : code}};
        if (npc != nullptr && stock != nullptr) {
            answer["stock"] = nlohmann::json::parse(npc->inventoryState);
        }
        if (const auto *inventory = Server::Get()->GetInventory().Find(player)) {
            answer["playerRevision"] = inventory->Revision();
        }
        reply.body = answer.dump();
        Framework::CoreModules::GetNetworkPeer()->SendRPC(reply, MafiaNet::ToGuid(incoming.peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void NpcInventoryModule::Update() {
        // Resource teardown destroys replicas without going through Npcs.
        std::erase_if(_records, [](const auto &entry) {
            return Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::NpcEntity>(entry.first) == nullptr;
        });
        std::vector<Incoming> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
        }
        for (const auto &entry : incoming) {
            Process(entry);
        }
        const auto now = MafiaNet::GetTimeUS() / 1000;
        std::vector<std::pair<std::uint64_t, nlohmann::json>> changes;
        std::vector<std::uint64_t> ready;
        for (auto &[id, record] : _records) {
            auto *npc = Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::NpcEntity>(id);
            if (npc == nullptr) {
                continue;
            }
            if (record->inventory.Holder() != 0 && (!CanAccess(record->inventory.Holder(), *npc) || !record->inventory.Holds(record->inventory.Holder(), record->inventory.Token(), npc->spawnEpoch, now))) {
                record->inventory.Release();
            }
            Publish(*npc, *record);
            // NPCs have no received/lost toast consumer.
            record->inventory.Stock().TakeNotices();
            for (const auto &change : record->inventory.Stock().TakeChanges()) {
                changes.emplace_back(id, KCDC::Features::Inventory::Inventory::EncodeChange(change));
            }
            if (record->inventory.Ready() && !record->announced) {
                record->announced = true;
                ready.push_back(id);
            }
        }
        auto harvests = std::move(_harvests);
        _harvests.clear();
        for (const auto &[id, player] : harvests) {
            if (Find(id) == nullptr || KCDC::Features::Player::Players::Find(player) == nullptr) {
                continue;
            }
            KCDC::EmitReservedEvent("npcHarvested", [id, player](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {KCDC::Scripting::WrapNpc(isolate, id), KCDC::Scripting::WrapPlayer(isolate, player)};
            });
        }
        for (const auto &[id, change] : changes) {
            if (Find(id) == nullptr) {
                continue;
            }
            KCDC::EmitReservedEvent("npcInventoryChanged", [id, &change](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {KCDC::Scripting::WrapNpc(isolate, id), KCDC::Scripting::InventoryValue(isolate, change)};
            });
        }
        for (const auto id : ready) {
            if (Find(id) == nullptr) {
                continue;
            }
            KCDC::EmitReservedEvent("npcInventoryReady", [id](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {KCDC::Scripting::WrapNpc(isolate, id)};
            });
        }
    }

    void NpcInventoryModule::Reset() {
        _records.clear();
        _harvests.clear();
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
    }
} // namespace KCDC::Features::Npc
