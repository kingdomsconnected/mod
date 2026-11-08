#include <utils/safe_win32.h>

#include "features/player/player_inventory_service.h"
#include "features/items/custom_item_module.h"

#include "core/application.h"
#include "features/ground_item/ground_item_service.h"
#include "features/player/player.h"
#include "features/ui/ui_hud_notifications.h"
#include "features/items/native_item_effects.h"

#include "shared/features/inventory/inventory_gameplay_message.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/inventory/inventory_sync.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/item_stack.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_reference_descriptor.h"
#include "sdk/wh/entitymodule/c_missile_weapon_runtime_data.h"
#include "sdk/wh/entitymodule/c_pickable_item.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/guimodule/c_ui_apse.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"
#include "sdk/wh/playermodule/c_player_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "features/player/player_service.h"
#include "features/items/native_item_use_result.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_item_holder.h"
#include "sdk/wh/entitymodule/c_item_pick_up_helper.h"
#include "sdk/wh/entitymodule/e_prepare_food.h"
#include "sdk/wh/entitymodule/i_action_actor_spec.h"
#include "sdk/wh/entitymodule/item_draw.h"
#include "sdk/wh/entitymodule/s_item_transfer_descriptor.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/guimodule/c_ui_apse_inventory.h"
#include "sdk/wh/rpgmodule/c_rpg_inventory.h"


namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "InventorySync";

        // Syncs waiting for the game thread; past this the backlog is dropped
        // and a full sync asked for instead.
        constexpr std::size_t kMaximumPendingSyncs = 256;
        constexpr std::size_t kMaximumWornItems    = 256;
        // One wear message; the server refuses longer ones.
        constexpr std::size_t kWearClaimsPerMessage = 64;
        // An armed use or shot adopts matching shrinkage for this long; long
        // enough for a drink that removes its bottle at the end of the sip.
        constexpr std::uint64_t kUseSpendWindowMs  = 8000;
        constexpr std::uint64_t kShotSpendWindowMs = 2000;
        // One spend message names at most this many units of one action.
        constexpr std::size_t kUnitsPerConsume    = 16;
        constexpr std::uint32_t kMaximumUseUnits  = 10;
        constexpr std::uint32_t kMaximumShotUnits = 16;
        // A spend the server never answers stops holding the projection.
        constexpr std::uint64_t kConsumeAnswerTimeoutMs = 10000;
        constexpr std::size_t kMaximumPendingConsumes   = 32;
        constexpr std::size_t kMaximumPendingRecoveries = 32;
        // The server answers a refused recovery with nothing, so this is how
        // long the shooter alone sees a round nobody else has. The approved
        // ground replica is placed on receipt and arrives within a round trip.
        constexpr std::uint64_t kRecoveryTimeoutMs = 5000;
        constexpr std::size_t kMaximumEquippedRows = 48;
        // Rows named in one acknowledgement as not shown; ids are at most 64
        // bytes, so this stays far inside the server's 16 KiB message cap.
        constexpr std::size_t kMaximumUnshownRows = 64;
        // Item names one chat notice lists before summing up the rest.
        constexpr std::size_t kMaximumAnnouncedNames = 5;

        struct InventoryRuntime {
            SDK::wh::entitymodule::C_Actor *actor         = nullptr;
            SDK::wh::rpgmodule::I_Soul *soul              = nullptr;
            SDK::wh::rpgmodule::C_InventorySoul *runtime  = nullptr;
            SDK::wh::entitymodule::C_Inventory *inventory = nullptr;
        };

        Shared::RPC::ItemClassId ToWireClass(const SDK::wh::entitymodule::S_ItemClassGUID &itemClass) {
            return KCDC::Features::Items::CustomItemModule::Get().ToLogical(itemClass);
        }

        InventoryRuntime ResolveLocalRuntime() {
            InventoryRuntime out;
            auto *framework = SDK::IGameFramework::GetInstance();
            out.actor       = static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
            out.soul        = out.actor != nullptr ? out.actor->GetSoul() : nullptr;
            out.runtime     = out.actor != nullptr ? out.actor->GetInventorySoulRuntime() : nullptr;
            if (out.soul == nullptr || out.runtime == nullptr || !out.runtime->IsInitialisedFor(out.soul)) {
                return out;
            }
            out.inventory = out.runtime->GetInventory();
            return out;
        }

        std::uint64_t NowMs() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        bool IsAmmunitionClass(const Shared::RPC::ItemClassId &itemClass) {
            const auto *entry = Shared::Game::FindMissileClass(itemClass);
            return entry != nullptr && !entry->weapon;
        }

        /** Food (drinks and potions are Food rows) and ointments: what item use spends. */
        bool IsUsableClass(const Shared::RPC::ItemClassId &itemClass) {
            const auto *entry = Shared::Game::FindItemClass(Shared::Game::CryGuidToString(itemClass.low, itemClass.high));
            return entry != nullptr && (entry->category == "Food" || entry->category == "Ointment");
        }

        /** The classes the server records as worn; anything else is the body's own business. */
        bool IsRecordableClass(const SDK::wh::entitymodule::C_Item &item) {
            const auto itemClass = item.InvokeGetItemClassRaw();
            const auto *entry    = Shared::Game::FindItemClass(Shared::Game::CryGuidToString(itemClass.m_low, itemClass.m_high));
            return entry != nullptr && Shared::Game::IsEquippableCategory(entry->category);
        }

        /** Each item the body wears, once: armor from the worn list, then the weapon slots. */
        void ForEachWorn(const SDK::wh::entitymodule::C_EquipmentManager &manager, const fu2::function<void(SDK::wh::entitymodule::C_Item *) const> &visit) {
            std::vector<const SDK::wh::entitymodule::C_Item *> seen;
            const auto once = [&](SDK::wh::entitymodule::C_Item *item) {
                if (item == nullptr || std::find(seen.begin(), seen.end(), item) != seen.end()) {
                    return;
                }
                seen.push_back(item);
                visit(item);
            };
            for (std::size_t index = 0; index < manager.GetEquippedCount(); ++index) {
                once(manager.GetEquippedAt(index));
            }
            for (std::size_t slot = 0; slot < SDK::wh::entitymodule::C_EquipmentManager::kWeaponSlotCount; ++slot) {
                once(manager.GetWeaponSlotItem(slot));
            }
        }

        bool IsWornBy(const SDK::wh::entitymodule::C_EquipmentManager &manager, const SDK::wh::entitymodule::C_Item *item) {
            bool worn = false;
            ForEachWorn(manager, [&](SDK::wh::entitymodule::C_Item *candidate) {
                worn = worn || candidate == item;
            });
            return worn;
        }

    } // namespace

    void PlayerInventoryService::Install() {
        KCDC::Features::Items::CustomItemModule::Install();
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::InventorySync>([](const KCDC::Shared::RPC::InventorySync &message, MafiaNet::Packet *) {
            if (!message.valid || KCDC::Core::gApplication == nullptr) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetPlayerInventoryService();
            std::scoped_lock lock(service._incomingMutex);
            if (service._pendingSync.size() >= kMaximumPendingSyncs) {
                // A full sync replaces everything queued before it.
                service._pendingSync.clear();
                service._pendingSync.push_back(R"({"overflow":true})");
            }
            service._pendingSync.push_back(message.body);
        });

        net->RegisterRPC<KCDC::Shared::RPC::InventoryConsumeResult>([](const KCDC::Shared::RPC::InventoryConsumeResult &message, MafiaNet::Packet *) {
            if (!message.valid || message.body.size() > 1024 || KCDC::Core::gApplication == nullptr) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetPlayerInventoryService();
            std::scoped_lock lock(service._incomingMutex);
            if (service._consumeAnswers.size() < kMaximumPendingConsumes) {
                service._consumeAnswers.push_back(message.body);
            }
        });
    }

    void PlayerInventoryService::Update() {
        if (_nativeContextChanged.exchange(false) && HasAuthority()) {
            _nativeDirty = true;
            _projected   = false;
            _projectionRetry.Reset();
            _equipment.Restart(NowMs());
            _wornAfterDress.clear();
            _contested.clear();
        }
        DrainSync();
        DrainConsumeAnswers();
        // After the combine that opened it has returned and dropped the kits
        // it emptied, and before anything projects.
        CloseNativeRepair();
        // Before the projection: a meal eaten or a round fired this frame is
        // reported and held, not put back.
        SettleConsumption();
        SendRecoveries();
        ReportWear();
        Project();
        ShowInventoryNotifications();
        ReconcileEquipment();
        // Moving a quick-access assignment need not equip or unequip an item.
        if (NowMs() >= _nextEquipmentPoll) {
            _nextEquipmentPoll = NowMs() + 250;
            _equipmentDirty    = true;
        }
        if (_projected && !_nativeDirty.load() && _equipment.CanReport() && _equipmentDirty.exchange(false)) {
            ReportEquipment();
        }

        std::string notice;
        {
            std::scoped_lock lock(_incomingMutex);
            notice.swap(_unsupportedNotice);
        }
        if (!notice.empty()) {
            KCDC::Core::gApplication->GetChatBox().AddMessage("Inventory", notice);
            KCDC::Features::UI::Hud::ShowNotification(notice);
        }
    }

    void PlayerInventoryService::Reset() {
        _foodUse       = {};
        _replayingFood = false;
        _foodEffects   = true;
        DiscardExpiredRecoveries(std::numeric_limits<std::uint64_t>::max());
        _localItemIds.clear();
        _nextLocalItemId      = 1;
        _authority            = false;
        _applyingAuthority    = false;
        _nativeDirty          = false;
        _nativeContextChanged = false;
        _awaitingFullSync     = false;
        _revision             = 0;
        _world.reset();
        _syncedWorld.reset();
        _projectionHold       = 0;
        _projectionPaused     = false;
        _projected            = false;
        _unshown.clear();
        _rows.clear();
        _notifications.Reset();
        _projection.Clear();
        _armedUseUntil  = 0;
        _armedShotUntil = 0;
        _armedUseItem   = 0;
        _consumeHold    = 0;
        _consumeRelease = 0;
        _pendingConsumes.clear();
        _openRepair     = {};
        _repairSettling = false;
        _sharpening     = false;
        _equipmentDirty = false;
        _equipment.Reset();
        _wornAfterDress.clear();
        _contested.clear();
        _nextEquipmentPoll = 0;
        _projectionRetry.Reset();

        std::scoped_lock lock(_incomingMutex);
        _consumeAnswers.clear();
        _pendingRecoveries.clear();
        _pendingSync.clear();
        _wornItems.clear();
        _unsupportedNotice.clear();
        _nextUnsupportedNotice = 0;
    }

    void PlayerInventoryService::NativeContextChanged() {
        _nativeContextChanged = true;
    }

    bool PlayerInventoryService::RefuseUnsupportedAction(const char *message) {
        if (!HasAuthority()) {
            return false;
        }
        const auto now = NowMs();
        std::scoped_lock lock(_incomingMutex);
        if (now >= _nextUnsupportedNotice) {
            _unsupportedNotice     = message;
            _nextUnsupportedNotice = now + 3000;
        }
        return true;
    }

    void PlayerInventoryService::NotifyNativeWear(std::uint64_t itemWuid) {
        if (!HasAuthority() || IsApplyingAuthority()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (_wornItems.size() < kMaximumWornItems) {
            _wornItems.insert(itemWuid);
        }
        else {
            _nativeDirty = true;
        }
    }

    void PlayerInventoryService::NotifyNativeChange() {
        if (HasAuthority() && !IsApplyingAuthority()) {
            _nativeDirty = true;
        }
    }

    void PlayerInventoryService::Acknowledge(bool applied, bool resync) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        KCDC::Shared::RPC::InventoryApplied answer;
        if (resync) {
            answer.body = nlohmann::json({{"resync", true}}).dump();
        }
        else {
            nlohmann::json body = {{"revision", _revision}, {"applied", applied}, {"definitionRevision", KCDC::Shared::Game::CustomItemRegistry::Get().Revision()}};
            if (!KCDC::Features::Items::CustomItemModule::Get().Failure().empty()) {
                body["definitionError"] = KCDC::Features::Items::CustomItemModule::Get().Failure();
            }
            if (applied && !_unshown.empty()) {
                auto ids = nlohmann::json::array();
                for (auto row = _unshown.begin(); row != _unshown.end() && ids.size() < kMaximumUnshownRows; ++row) {
                    ids.push_back(row->first);
                }
                body["unshown"] = std::move(ids);
            }
            answer.body = body.dump();
        }
        net->BroadcastRPC(answer, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PlayerInventoryService::DrainSync() {
        std::vector<std::string> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_pendingSync);
        }
        for (const auto &body : incoming) {
            const auto message = nlohmann::json::parse(body, nullptr, false);
            if (!message.is_object()) {
                continue;
            }
            _authority = true;
            std::optional<std::uint32_t> world;
            if (const auto found = message.find("world"); found != message.end() && found->is_number_unsigned() && found->get<std::uint64_t>() <= std::numeric_limits<std::uint32_t>::max()) {
                world        = found->get<std::uint32_t>();
                _syncedWorld = world;
            }
            if (message.value("overflow", false)) {
                _awaitingFullSync = true;
                Acknowledge(false, true);
                continue;
            }
            const bool full = message.value("full", false);
            if (_awaitingFullSync && !full) {
                continue;
            }
            if (KCDC::Shared::Inventory::ApplySync(message, _revision, _rows)) {
                _world = world;
                _notifications.Queue(message);
                _projected        = false;
                _awaitingFullSync = false;
                _nativeDirty      = true;
                _projectionRetry.Reset();
                if (const auto equipped = message.find("equipped"); equipped != message.end() && equipped->is_object()) {
                    std::map<std::string, std::uint32_t> record;
                    for (const auto &[row, count] : equipped->items()) {
                        if (count.is_number_unsigned() && record.size() < kMaximumEquippedRows) {
                            record.emplace(row, count.get<std::uint32_t>());
                        }
                    }
                    std::optional<std::uint64_t> restore;
                    if (const auto generation = message.find("equipmentRestore"); generation != message.end() && generation->is_number_unsigned()) {
                        restore = generation->get<std::uint64_t>();
                    }
                    // A restore replaces the serving too. Ignore any approval
                    // still queued for the old native inventory.
                    const bool newRestore = restore != _equipment.Restore();
                    if (newRestore) {
                        _foodUse = {};
                    }
                    std::optional<Shared::Inventory::Quickslots> quickslots;
                    if (const auto slots = message.find("quickslots"); slots != message.end()) {
                        quickslots.emplace();
                        if (!Shared::Inventory::Quickslots::Read(*slots, *quickslots)) {
                            _awaitingFullSync = true;
                            Acknowledge(false, true);
                            continue;
                        }
                    }
                    _equipment.Receive(std::move(record), restore, NowMs(), std::move(quickslots));
                    // Every publication carries the outfit; only a new restore starts the layering
                    // contest again, or one unrelated change would let a declined piece back on.
                    if (newRestore) {
                        _wornAfterDress.clear();
                        _contested.clear();
                    }
                }
            }
            else if (!_awaitingFullSync) {
                Framework::Logging::GetLogger(kLogger)->warn("Inventory sync did not fit revision {}; asking for the whole inventory", _revision);
                _awaitingFullSync = true;
                Acknowledge(false, true);
            }
        }
    }

    void PlayerInventoryService::ReportWear() {
        // The grindstone raises health as it turns; that waits for it to stop.
        const SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers != nullptr && blockers->IsHeld(SDK::wh::guimodule::E_UIState::MinigameSharpening)) {
            _sharpening = true;
            return;
        }
        // A repair splits rows under projected stacks, so a stack's rows are
        // not known again until the projection has rebound them.
        const bool repairing = std::any_of(_pendingConsumes.begin(), _pendingConsumes.end(), [](const auto &pending) {
            return !pending.second.repaired.empty();
        });
        if (repairing || _repairSettling) {
            return;
        }
        const bool sharpened = std::exchange(_sharpening, false);
        std::set<std::uint64_t> worn;
        {
            std::scoped_lock lock(_incomingMutex);
            worn.swap(_wornItems);
        }
        if (worn.empty() || !HasAuthority()) {
            return;
        }
        const InventoryRuntime runtime = ResolveLocalRuntime();
        std::vector<KCDC::Features::Items::NativeWearClaim> claims;
        for (const auto wuid : worn) {
            if (_projection.TakeWear(runtime.inventory, wuid, claims)) {
                continue;
            }
            KCDC::Features::Items::NativeRepairPart sharpening;
            if (sharpened && _projection.TakeSharpening(runtime.inventory, wuid, sharpening)) {
                SubmitConsume({{"kind", "sharpen"}, {"equipment", {{"id", sharpening.rowId}, {"before", sharpening.before}, {"after", sharpening.after}}}, {"equipped", sharpening.equipped}}, {wuid});
                continue;
            }
            // Not wear: a repair or a change the server did not make. The
            // next projection writes the server's properties back.
            _nativeDirty = true;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        for (std::size_t first = 0; net != nullptr && first < claims.size(); first += kWearClaimsPerMessage) {
            nlohmann::json batch = nlohmann::json::array();
            for (std::size_t index = first; index < std::min(claims.size(), first + kWearClaimsPerMessage); ++index) {
                batch.push_back({{"id", claims[index].id}, {"metadata", claims[index].metadata}});
            }
            KCDC::Shared::RPC::InventoryWear message;
            message.body = nlohmann::json({{"claims", std::move(batch)}}).dump();
            net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void PlayerInventoryService::Project() {
        // A held projection keeps its dirty flag and runs once released.
        if (!HasAuthority() || _awaitingFullSync || _projectionPaused || !_foodUse.request.empty() || _revision < _projectionHold || _revision < _consumeHold) {
            return;
        }
        // Nor under the grindstone, whose health changes ReportWear holds.
        const SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers != nullptr && blockers->IsHeld(SDK::wh::guimodule::E_UIState::MinigameSharpening)) {
            return;
        }
        const auto now = NowMs();
        if (!_projectionRetry.CanAttempt(now) || (!_nativeDirty.exchange(false) && !_projectionRetry.Pending())) {
            return;
        }
        const InventoryRuntime runtime = ResolveLocalRuntime();
        if (runtime.inventory == nullptr) {
            // No body to project onto yet; the next frame tries again.
            _nativeDirty = true;
            return;
        }
        std::string error;
        std::map<std::string, std::string> unshown;
        bool applied = false;
        {
            AuthorityProjection applying(*this);
            // A worn row that left -- looted, dropped, taken by a script --
            // comes off through the player's funnel first, as a stale piece
            // does in DressFromServer. The removal alone reaches the body
            // only through the raw UnequipItemIfEquipped (0x1808F7870),
            // which skips the quick-access path, and not at all while
            // another holder has the item: C_ItemHolder::RemoveItem
            // (0x180479758) then tells the inventory only of ownership.
            auto *manager = runtime.runtime->GetEquipmentManager();
            applied       = _projection.Apply(runtime.inventory, _rows, unshown, error, [manager](SDK::wh::entitymodule::C_Item &item) {
                if (manager != nullptr && IsWornBy(*manager, &item)) {
                    manager->InvokeUnequipPlayersItem(SDK::wh::entitymodule::CreateItemReferenceDescriptor(&item));
                }
            });
        }
        _projected = applied;
        if (applied) {
            _repairSettling = false;
            // A row the game cannot show is not retried on a timer: the
            // projection tries its kind again once the rows of it change.
            _projectionRetry.Reset();
            Framework::Logging::GetLogger(kLogger)->debug("Projected inventory revision {} ({} rows, {} not shown)", _revision, _rows.size(), unshown.size());
            ReportUnshown(std::move(unshown));
        }
        else {
            _projectionRetry.Failed(now);
            Framework::Logging::GetLogger(kLogger)->error("Inventory revision {} could not be projected ({}): {}", _revision, _projectionRetry.Pending() ? "retry scheduled" : "retry limit reached", error);
        }
        Acknowledge(applied, false);
    }

    void PlayerInventoryService::ShowInventoryNotifications() {
        if (!_projected || _nativeDirty.load() || _awaitingFullSync) {
            return;
        }
        _notifications.Flush([this](const std::string &itemClass, int amount) {
            // A class the game does not know, or could not show, already has
            // its own explanation; a receipt for it would announce nothing.
            if (KCDC::Shared::Game::FindItemClass(itemClass) == nullptr) {
                return true;
            }
            if (amount > 0) {
                for (const auto &[id, reason] : _unshown) {
                    const auto row = _rows.find(id);
                    if (row != _rows.end() && row->second.at("item") == itemClass) {
                        return true;
                    }
                }
            }
            return KCDC::Features::UI::Hud::ShowItemTransfer(KCDC::Shared::Game::ItemClassIdFromGuid(itemClass), amount);
        });
    }

    void PlayerInventoryService::ReportUnshown(std::map<std::string, std::string> unshown) {
        // Every apply reports the rows it left out, so only a row that newly
        // failed is logged and announced.
        std::set<std::string> names;
        for (const auto &[id, reason] : unshown) {
            const auto previous = _unshown.find(id);
            if (previous != _unshown.end() && previous->second == reason) {
                continue;
            }
            const auto row             = _rows.find(id);
            const std::string guid     = row != _rows.end() ? row->second.value("item", std::string()) : std::string();
            const auto *definition     = Shared::Game::FindItemClass(guid);
            const std::string itemName = definition != nullptr ? std::string(definition->name) : guid;
            Framework::Logging::GetLogger(kLogger)->error("Inventory row {} ({} {}) could not be shown: {}", id, itemName, guid, reason);
            names.insert(itemName);
        }
        _unshown = std::move(unshown);
        if (names.empty()) {
            return;
        }
        // One line however many fail; the log names every row.
        std::string message = "Your game could not show: ";
        std::size_t listed  = 0;
        for (const auto &name : names) {
            if (listed == kMaximumAnnouncedNames) {
                message += std::format(" and {} more", names.size() - listed);
                break;
            }
            message += listed == 0 ? name : ", " + name;
            ++listed;
        }
        KCDC::Core::gApplication->GetChatBox().AddMessage("Inventory", message + ". Everything else is in your inventory.");
    }

    void PlayerInventoryService::QueueAmmunitionRecovery(const Shared::RPC::ItemClassId &fired, const Shared::RPC::ItemClassId &landed, float x, float y, float z, std::uint64_t nativeItemWuid, bool laidOnGround) {
        if (!HasAuthority()) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        if (nativeItemWuid != 0) {
            _recoveringItems.emplace(nativeItemWuid, RecoveringItem {landed, NowMs() + kRecoveryTimeoutMs});
        }
        if (_pendingRecoveries.size() < kMaximumPendingRecoveries) {
            _pendingRecoveries.push_back({fired, landed, {x, y, z}, nativeItemWuid, laidOnGround});
        }
    }

    SDK::wh::entitymodule::C_Item *PlayerInventoryService::TakeRecoveredAmmunition(std::uint64_t nativeItemWuid, const Shared::RPC::InventoryItem &item) {
        std::scoped_lock lock(_incomingMutex);
        const auto pending = _recoveringItems.find(nativeItemWuid);
        if (pending == _recoveringItems.end() || pending->second.itemClass != item.itemClass || item.amount != 1) {
            return nullptr;
        }
        auto *native = KCDC::Features::GroundItem::FindItem(nativeItemWuid);
        auto *world  = KCDC::Features::GroundItem::WorldInventory();
        if (native == nullptr || world == nullptr || native->GetOwner() != world || native->GetPickableItem() == nullptr || native->GetAmount() != 1 || !KCDC::Features::GroundItem::SameItemClass(item.itemClass, native->InvokeGetItemClassRaw())) {
            return nullptr;
        }
        _recoveringItems.erase(pending);
        return native;
    }

    void PlayerInventoryService::DiscardExpiredRecoveries(std::uint64_t now) {
        std::vector<std::uint64_t> expired;
        {
            std::scoped_lock lock(_incomingMutex);
            for (auto pending = _recoveringItems.begin(); pending != _recoveringItems.end();) {
                if (pending->second.expiresAt > now) {
                    ++pending;
                    continue;
                }
                expired.push_back(pending->first);
                pending = _recoveringItems.erase(pending);
            }
        }
        // Native removal can report inventory changes through the hooks.
        // Retire the bookkeeping under the lock, then release the items.
        for (const auto nativeItemWuid : expired) {
            auto *native = KCDC::Features::GroundItem::FindItem(nativeItemWuid);
            auto *world  = KCDC::Features::GroundItem::WorldInventory();
            if (native != nullptr && world != nullptr && native->GetOwner() == world) {
                world->RemoveItem(native);
            }
        }
    }

    void PlayerInventoryService::ArmConsumption(ConsumptionKind kind, std::uint64_t nativeId) {
        if (!HasAuthority()) {
            return;
        }
        if (kind == ConsumptionKind::Use) {
            _armedUseItem = nativeId;
        }
        const auto now      = NowMs();
        auto &until         = kind == ConsumptionKind::Use ? _armedUseUntil : _armedShotUntil;
        const auto deadline = now + (kind == ConsumptionKind::Use ? kUseSpendWindowMs : kShotSpendWindowMs);
        auto current        = until.load();
        while (current < deadline && !until.compare_exchange_weak(current, deadline)) {}
    }

    bool PlayerInventoryService::InterceptFoodUse(SDK::wh::entitymodule::C_Item *item, const KCDC::Features::Items::NativeFoodUse &use) {
        if (_replayingFood) {
            return false;
        }
        auto *owner = Human::FindLocal();
        auto *net   = Framework::CoreModules::GetNetworkPeer();
        if (!_foodUse.request.empty() || !_world || !CanSpend() || _projectionPaused || _revision < _consumeHold || owner == nullptr || !owner->soul.CanAct() || net == nullptr || !ManagesNativeItem(item)) {
            return true;
        }
        const auto units = UnitsOf(item->GetId());
        if (units.empty()) {
            return true;
        }
        _foodUse.request     = std::to_string(_nextConsumeRequest++);
        _foodUse.nativeId    = item->GetId();
        _foodUse.deadline    = NowMs() + kConsumeAnswerTimeoutMs;
        _foodUse.incarnation = owner->soul.incarnation;
        _foodUse.world       = *_world;
        _foodUse.native      = use;
        KCDC::Shared::RPC::InventoryConsume request;
        request.body = nlohmann::json({{"kind", "foodRequest"}, {"request", _foodUse.request}, {"rowId", units.front().id}, {"revision", _revision}, {"incarnation", _foodUse.incarnation}, {"world", _foodUse.world}}).dump();
        net->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool PlayerInventoryService::AcceptFoodAnswer(const nlohmann::json &answer) {
        if (_foodUse.request.empty() || answer.at("request") != _foodUse.request) {
            return false;
        }
        const auto pending  = _foodUse;
        const auto runtime  = ResolveLocalRuntime();
        auto *owner         = Human::FindLocal();
        auto *item          = KCDC::Features::GroundItem::FindItem(pending.nativeId);
        bool eaten          = false;
        const bool accepted = answer.value("ok", false);
        if (accepted && IsSynced() && !_projectionPaused && NowMs() <= pending.deadline && owner != nullptr && owner->soul.incarnation == pending.incarnation && _world == pending.world && owner->soul.CanAct() && runtime.actor != nullptr && item != nullptr
            && item->GetOwner() == runtime.inventory && ManagesNativeItem(item)) {
            _foodEffects   = answer.value("nativeEffects", true);
            _replayingFood = true;
            _foodEaten      = false;
            const auto &use = pending.native;
            if (use.source == KCDC::Features::Items::NativeFoodUse::Source::Inventory) {
                // Resolve the current page; never retain a GUI pointer across a reply.
                if (auto *apse = SDK::wh::guimodule::C_UIApse::GetInstance()) {
                    apse->GetInventory()->UseConsumableItem(item, use.saveConfirmed);
                }
                eaten = _foodEaten;
            }
            else if (use.source == KCDC::Features::Items::NativeFoodUse::Source::Player) {
                eaten = static_cast<SDK::wh::entitymodule::C_Player *>(runtime.actor)->UseConsumableItem(item, use.amount, use.context, use.checkFood, use.saveSlot);
            }
            else {
                eaten = runtime.actor->UseFood(item, use.amount, use.context, use.checkFood);
            }
            _replayingFood = false;
            _foodEffects   = true;
        }
        // The native use already spends its unit. Do not arm the legacy
        // post-use path: the server debited this reservation before replying.
        _foodUse     = {};
        _nativeDirty = true;
        if (accepted) {
            _consumeRelease = std::max(_consumeRelease, answer.value("revision", std::uint64_t {0}));
            KCDC::Shared::RPC::InventoryConsume receipt;
            receipt.body = nlohmann::json({{"kind", "foodReceipt"}, {"request", pending.request}, {"eaten", eaten}}).dump();
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(receipt, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        return true;
    }

    void PlayerInventoryService::ObserveNativeRepair(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment) {
        if (!HasAuthority() || (!ManagesNativeItem(kit) && !ManagesNativeItem(equipment))) {
            return;
        }
        // A selection can combine several items and reuse a surviving kit.
        // Capture each native unit once, before its first change this frame.
        const bool canSpend = CanSpend() && _revision >= _consumeHold && ManagesNativeItem(kit) && ManagesNativeItem(equipment);
        _openRepair.Observe(kit != nullptr ? kit->GetId() : 0, equipment != nullptr ? equipment->GetId() : 0, equipment != nullptr && equipment->IsEquipped(), canSpend,
            [&](std::uint64_t id, const std::vector<KCDC::Features::Items::NativeRepairPart> &taken, KCDC::Features::Items::NativeRepairPart &part) {
                return _projection.ReadRepairStart(id == equipment->GetId() ? *equipment : *kit, taken, part);
            });
    }

    void PlayerInventoryService::CaptureNativeRepairResult(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment) {
        if (!HasAuthority() || (!ManagesNativeItem(kit) && !ManagesNativeItem(equipment))) {
            return;
        }
        _openRepair.CaptureResult(kit != nullptr ? kit->GetId() : 0, equipment != nullptr ? equipment->GetId() : 0, [&](bool kitPart, KCDC::Features::Items::NativeRepairPart &part) {
            return KCDC::Features::Items::NativeInventoryProjection::ReadRepairResult(kitPart ? *kit : *equipment, kitPart, part);
        });
    }

    void PlayerInventoryService::ReportPreparation(const char *kind, const std::vector<std::uint64_t> &stacks, std::int32_t count) {
        if (!HasAuthority()) {
            return;
        }
        // The game takes each stack whole until `count` runs out.
        std::uint32_t left = count < 0 ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(count);
        std::map<std::string, std::uint32_t> taken;
        for (const auto wuid : stacks) {
            for (const auto &unit : UnitsOf(wuid)) {
                const auto take = std::min(unit.amount, left);
                if (take != 0) {
                    taken[unit.id] += take;
                }
                left -= take;
            }
        }
        nlohmann::json units = nlohmann::json::array();
        for (const auto &[id, amount] : taken) {
            units.push_back({{"id", id}, {"amount", amount}});
        }
        if (!CanSpend() || units.empty()) {
            RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
            _nativeDirty = true;
            return;
        }
        SubmitConsume({{"kind", kind}, {"units", std::move(units)}});
    }

    void PlayerInventoryService::CloseNativeRepair() {
        if (_openRepair.Empty()) {
            return;
        }
        auto repair = std::move(_openRepair);
        _openRepair = {};
        if (!repair.Complete()) {
            Framework::Logging::GetLogger(kLogger)->warn("A native repair could not be reported ({}); projecting it back", repair.Refused() ? "not described" : "result unreadable or unchanged");
            RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
            DiscardRepairWear(repair.Touched());
            _nativeDirty = true;
            return;
        }
        SubmitConsume(repair.Encode(), repair.Touched());
    }

    void PlayerInventoryService::DiscardRepairWear(const std::vector<std::uint64_t> &repaired) {
        // What the repair did to its kits and equipment is reported by the
        // repair itself, never as wear.
        std::scoped_lock lock(_incomingMutex);
        for (const auto wuid : repaired) {
            _wornItems.erase(wuid);
        }
    }

    void PlayerInventoryService::SubmitConsume(nlohmann::json body, std::vector<std::uint64_t> repaired) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        const auto request = std::to_string(_nextConsumeRequest++);
        body["request"]    = request;
        KCDC::Shared::RPC::InventoryConsume message;
        message.body = body.dump();
        net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        _pendingConsumes.insert_or_assign(request, PendingConsume {NowMs() + kConsumeAnswerTimeoutMs, std::move(repaired)});
        _consumeHold = std::numeric_limits<std::uint64_t>::max();
    }

    void PlayerInventoryService::SettleConsumption() {
        const auto now  = NowMs();
        const bool use  = now <= _armedUseUntil.load();
        const bool shot = now <= _armedShotUntil.load();
        if ((!use && !shot) || !CanSpend()) {
            return;
        }
        const InventoryRuntime runtime = ResolveLocalRuntime();
        const std::uint64_t useItem    = _armedUseItem.load();
        const auto spent               = _projection.TakeConsumption(runtime.inventory, [use, shot, useItem](std::uint64_t nativeId, const Shared::RPC::ItemClassId &itemClass) {
            return (shot && IsAmmunitionClass(itemClass)) || (use && nativeId == useItem && IsUsableClass(itemClass));
        });
        if (spent.empty() || Framework::CoreModules::GetNetworkPeer() == nullptr) {
            return;
        }
        // One message per kind, split only where one would exceed the
        // server's per-action bound: a scatter shot, a stacked meal.
        for (const bool shots : {false, true}) {
            const std::uint32_t maximum = shots ? kMaximumShotUnits : kMaximumUseUnits;
            nlohmann::json units        = nlohmann::json::array();
            std::uint32_t amount        = 0;
            const auto flush            = [&]() {
                if (units.empty()) {
                    return;
                }
                SubmitConsume({{"kind", shots ? "shot" : "use"}, {"revision", _revision}, {"units", units}});
                units  = nlohmann::json::array();
                amount = 0;
            };
            for (const auto &consumption : spent) {
                if (IsAmmunitionClass(consumption.itemClass) != shots) {
                    continue;
                }
                for (auto unit : consumption.units) {
                    while (unit.amount != 0) {
                        if (amount == maximum || units.size() == kUnitsPerConsume) {
                            flush();
                        }
                        const auto take = std::min(unit.amount, maximum - amount);
                        units.push_back({{"id", unit.id}, {"amount", take}});
                        amount += take;
                        unit.amount -= take;
                    }
                }
            }
            flush();
        }
    }

    void PlayerInventoryService::DrainConsumeAnswers() {
        std::vector<std::string> answers;
        {
            std::scoped_lock lock(_incomingMutex);
            answers.swap(_consumeAnswers);
        }
        for (const auto &body : answers) {
            const auto answer = nlohmann::json::parse(body, nullptr, false);
            if (!answer.is_object() || !answer.contains("request") || !answer.at("request").is_string()) {
                continue;
            }
            if (AcceptFoodAnswer(answer)) {
                continue;
            }
            const auto pending = _pendingConsumes.find(answer.at("request").get<std::string>());
            if (pending == _pendingConsumes.end()) {
                continue;
            }
            if (!pending->second.repaired.empty()) {
                DiscardRepairWear(pending->second.repaired);
                _repairSettling = true;
                _equipmentDirty = true;
            }
            _pendingConsumes.erase(pending);
            if (answer.value("ok", false)) {
                // The projection waits for the revision the spend landed in.
                if (const auto revision = answer.find("revision"); revision != answer.end() && revision->is_number_unsigned()) {
                    _consumeRelease = std::max(_consumeRelease, revision->get<std::uint64_t>());
                }
                continue;
            }
            // Refused: the next projection puts the spent units back.
            Framework::Logging::GetLogger(kLogger)->warn("The server refused a spend: {}", answer.value("code", std::string("unknown")));
            _nativeDirty = true;
        }
        const auto now = NowMs();
        if (!_foodUse.request.empty() && now > _foodUse.deadline) {
            _foodUse     = {};
            _nativeDirty = true;
        }
        for (auto pending = _pendingConsumes.begin(); pending != _pendingConsumes.end();) {
            if (now < pending->second.deadline) {
                ++pending;
                continue;
            }
            Framework::Logging::GetLogger(kLogger)->warn("No server answer to a spend; projecting again");
            if (!pending->second.repaired.empty()) {
                DiscardRepairWear(pending->second.repaired);
                _repairSettling = true;
                _equipmentDirty = true;
            }
            _nativeDirty = true;
            pending      = _pendingConsumes.erase(pending);
        }
        if (_pendingConsumes.empty()) {
            _consumeHold = _consumeRelease;
        }
    }

    void PlayerInventoryService::SendRecoveries() {
        DiscardExpiredRecoveries(NowMs());
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || !IsSynced()) {
            return;
        }
        std::vector<PendingRecovery> recoveries;
        {
            std::scoped_lock lock(_incomingMutex);
            recoveries.swap(_pendingRecoveries);
        }
        for (const auto &recovery : recoveries) {
            KCDC::Shared::RPC::InventoryRecover message;
            nlohmann::json body {{"item", Shared::Game::CryGuidToString(recovery.fired.low, recovery.fired.high)}, {"landed", Shared::Game::CryGuidToString(recovery.landed.low, recovery.landed.high)},
                {"position", {recovery.position[0], recovery.position[1], recovery.position[2]}}};
            if (recovery.nativeItemWuid != 0) {
                auto *native   = KCDC::Features::GroundItem::FindItem(recovery.nativeItemWuid);
                auto *pickable = native != nullptr ? native->GetPickableItem() : nullptr;
                auto *entity   = pickable != nullptr ? pickable->GetEntity() : nullptr;
                // Collision finishes positioning/embedding the pickable after
                // the recovery hook returns. Sample that final pose here. When
                // the native item is already gone, the impact point still
                // lets the server lay the round down as a fresh stack. A round
                // laid on the ground keeps that position; the adopted pickable
                // is moved there with the server's pose.
                if (entity != nullptr) {
                    const auto position = entity->GetWorldPos();
                    const auto rotation = entity->GetWorldRotation();
                    if (!recovery.laidOnGround) {
                        body["position"] = {position.x, position.y, position.z};
                    }
                    body["rotation"]       = {rotation.w, rotation.x, rotation.y, rotation.z};
                    body["nativeItemWuid"] = recovery.nativeItemWuid;
                }
            }
            message.body = body.dump();
            net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    std::vector<KCDC::Features::Items::NativeUnit> PlayerInventoryService::UnwornUnitsOfKind(SDK::wh::entitymodule::C_Item &item) const {
        auto units      = _projection.UnitsOfKind(item);
        const auto worn = NativeEquipment();
        for (auto &unit : units) {
            const auto held = worn.find(unit.id);
            unit.amount -= held != worn.end() ? std::min(held->second, unit.amount) : 0;
        }
        std::erase_if(units, [](const KCDC::Features::Items::NativeUnit &unit) {
            return unit.amount == 0;
        });
        return units;
    }

    std::map<std::string, std::uint32_t> PlayerInventoryService::NativeEquipment() const {
        std::map<std::string, std::uint32_t> rows;
        const InventoryRuntime runtime = ResolveLocalRuntime();
        auto *manager                  = runtime.runtime != nullptr ? runtime.runtime->GetEquipmentManager() : nullptr;
        if (manager == nullptr) {
            return rows;
        }
        ForEachWorn(*manager, [&](SDK::wh::entitymodule::C_Item *item) {
            if (!IsRecordableClass(*item)) {
                return;
            }
            // Capped at what the stack holds now: a round fired this frame is
            // already gone natively but still in the last projection.
            std::uint32_t left = item->GetAmount();
            for (const auto &unit : _projection.UnitsOf(item->GetId())) {
                if (left == 0 || (rows.size() >= kMaximumEquippedRows && !rows.contains(unit.id))) {
                    break;
                }
                const auto take = std::min(unit.amount, left);
                rows[unit.id] += take;
                left -= take;
            }
        });
        return rows;
    }

    std::map<std::string, std::uint32_t> PlayerInventoryService::RecordedEquipment() const {
        // A recorded piece the game could not make cannot be on the body,
        // but it is still what the owner wears: kept in the record, the
        // outfit neither waits on it forever nor loses it in the next report.
        auto rows = NativeEquipment();
        for (const auto &[id, count] : _equipment.Desired()) {
            if (_unshown.contains(id) && rows.size() < kMaximumEquippedRows) {
                rows.emplace(id, count);
            }
        }
        return rows;
    }

    std::optional<Shared::Inventory::Quickslots> PlayerInventoryService::RecordedQuickslots() const {
        const auto runtime    = ResolveLocalRuntime();
        auto *playerInventory = runtime.inventory != nullptr ? runtime.runtime->GetPlayerInventorySoul() : nullptr;
        auto *outfits         = playerInventory != nullptr ? playerInventory->GetOutfitManager() : nullptr;
        const auto *state     = outfits != nullptr ? outfits->GetOutfitState(outfits->GetCurrentOutfitId()) : nullptr;
        if (state == nullptr) {
            return std::nullopt;
        }
        Shared::Inventory::Quickslots result;
        const auto &desired  = _equipment.DesiredQuickslots();
        const auto reference = [&](SDK::wh::entitymodule::C_Item *item, const std::string &preferred) {
            if (item == nullptr) {
                return std::string {};
            }
            const auto units = _projection.UnitsOf(item->GetId());
            // Several persistent rows can project to the same native stack.
            // Retain the saved reference as long as that row is still present.
            for (const auto &unit : units) {
                if (unit.id == preferred && unit.amount != 0) {
                    return unit.id;
                }
            }
            for (const auto &unit : units) {
                if (unit.amount != 0) {
                    return unit.id;
                }
            }
            return std::string {};
        };
        for (std::uint32_t group = 0; group < 2; ++group) {
            const auto *manager = state->GetQuickAccessManager(group);
            auto &active        = group == 0 ? result.activeWeapon : result.activeItem;
            active              = std::min(manager->GetActiveSlotIndex(), 3U);
            for (std::uint32_t slot = 0; slot < Shared::Inventory::Quickslots::kSlotCount; ++slot) {
                const auto *nativeSlot = manager->GetSlot(slot);
                if (nativeSlot == nullptr) {
                    continue;
                }
                if (group == 0) {
                    for (std::uint32_t part = 0; part < 2; ++part) {
                        result.weapons[slot][part] = reference(nativeSlot->GetItem(part), desired ? desired->weapons[slot][part] : std::string {});
                    }
                }
                else {
                    result.items[slot] = reference(nativeSlot->GetItem(0), desired ? desired->items[slot] : std::string {});
                }
            }
        }
        return _equipment.RecordQuickslots(std::move(result), [&](const std::string &id) {
            return _unshown.contains(id);
        });
    }

    void PlayerInventoryService::RestoreQuickslots(const std::vector<std::uint64_t> &wanted) {
        const auto &desired = _equipment.DesiredQuickslots();
        if (!desired) {
            return;
        }
        const bool rebuild = RecordedQuickslots() != desired;
        const auto runtime    = ResolveLocalRuntime();
        auto *playerInventory = runtime.inventory != nullptr ? runtime.runtime->GetPlayerInventorySoul() : nullptr;
        auto *outfits         = playerInventory != nullptr ? playerInventory->GetOutfitManager() : nullptr;
        auto *state           = outfits != nullptr ? outfits->GetOutfitState(outfits->GetCurrentOutfitId()) : nullptr;
        if (state == nullptr) {
            return;
        }
        // Validate native eligibility and capacity before changing either menu.
        // The native setters assume valid indices and compatible item classes.
        for (std::uint32_t group = 0; group < 2; ++group) {
            auto *manager        = state->GetQuickAccessManager(group);
            const auto active    = group == 0 ? desired->activeWeapon : desired->activeItem;
            const auto &selected = group == 0 ? desired->weapons[active][0] : desired->items[active];
            if (manager->GetSlot(active) == nullptr && active != 0 && !_unshown.contains(selected)) {
                return;
            }
            for (std::uint32_t slot = 0; slot < Shared::Inventory::Quickslots::kSlotCount; ++slot) {
                for (std::uint32_t part = 0; part < (group == 0 ? 2U : 1U); ++part) {
                    const auto &id = group == 0 ? desired->weapons[slot][part] : desired->items[slot];
                    if (id.empty() || _unshown.contains(id)) {
                        continue;
                    }
                    auto *item = runtime.inventory->FindOwnedItemById(_projection.NativeIdOf(id));
                    if (manager->GetSlot(slot) == nullptr || item == nullptr || outfits->GetQuickAccessManagerForItem(item) != manager) {
                        return;
                    }
                }
            }
        }
        auto *equipment = runtime.runtime->GetEquipmentManager();
        if (equipment == nullptr) {
            return;
        }
        // ClearAllSlots also unequips every native weapon class. QAM replay
        // only equips selected/paired weapons, so retain the other worn sets.
        auto worn = wanted;
        for (std::size_t slot = 0; slot < SDK::wh::entitymodule::C_EquipmentManager::kWeaponSlotCount; ++slot) {
            const auto *item = equipment->GetWeaponSlotItem(slot);
            if (item != nullptr && std::find(worn.begin(), worn.end(), item->GetId()) == worn.end()) {
                worn.push_back(item->GetId());
            }
        }
        for (std::uint32_t group = 0; rebuild && group < 2; ++group) {
            auto *manager = state->GetQuickAccessManager(group);
            manager->ClearAllSlots();
            for (std::uint32_t slot = 0; slot < Shared::Inventory::Quickslots::kSlotCount; ++slot) {
                for (std::uint32_t part = 0; part < (group == 0 ? 2U : 1U); ++part) {
                    const auto &id = group == 0 ? desired->weapons[slot][part] : desired->items[slot];
                    if (id.empty() || _unshown.contains(id)) {
                        continue;
                    }
                    // A previous assignment may merge or replace native items.
                    auto *item = runtime.inventory->FindOwnedItemById(_projection.NativeIdOf(id));
                    if (item != nullptr && outfits->GetQuickAccessManagerForItem(item) == manager) {
                        manager->SetItem(item, slot);
                    }
                }
            }
            const auto active = group == 0 ? desired->activeWeapon : desired->activeItem;
            if (manager->GetSlot(active) != nullptr) {
                manager->SelectSlot(active, true);
            }
        }
        // The QAM now owns the restored assignments and selection. Restore
        // missing equipment through the raw native equip path: the player's
        // default funnel would assign these weapons to quickslot zero again.
        for (const auto id : worn) {
            auto *item = runtime.inventory->FindOwnedItemById(id);
            if (item != nullptr && !IsWornBy(*equipment, item)) {
                equipment->InvokeEquipItemRaw(item);
            }
        }
    }

    void PlayerInventoryService::ReportEquipment() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || !CanSpend()) {
            _equipmentDirty = true;
            return;
        }
        auto current    = RecordedEquipment();
        auto quickslots = RecordedQuickslots();
        if (!quickslots || (current == _equipment.Sent() && quickslots == _equipment.SentQuickslots())) {
            return;
        }
        KCDC::Shared::RPC::InventoryEquipment message;
        nlohmann::json body {{"revision", _revision}, {"items", current}};
        body["quickslots"] = quickslots->Encode();
        if (const auto restore = _equipment.Restore()) {
            body["equipmentRestore"] = *restore;
        }
        message.body = body.dump();
        net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        _equipment.Reported(std::move(current), NowMs(), std::move(quickslots));
    }

    void PlayerInventoryService::DressFromServer() {
        const InventoryRuntime runtime = ResolveLocalRuntime();
        auto *manager                  = runtime.runtime != nullptr ? runtime.runtime->GetEquipmentManager() : nullptr;
        if (manager == nullptr || runtime.inventory == nullptr) {
            return;
        }
        std::vector<std::uint64_t> wanted;
        for (const auto &[row, count] : _equipment.Desired()) {
            auto *item = runtime.inventory->FindOwnedItemById(_projection.NativeIdOf(row));
            if (item != nullptr && std::find(wanted.begin(), wanted.end(), item->GetId()) == wanted.end()) {
                wanted.push_back(item->GetId());
            }
        }
        // What the record does not name comes off first, so a recorded piece
        // never contends with a stale one for its slot.
        std::vector<std::uint64_t> stale;
        ForEachWorn(*manager, [&](SDK::wh::entitymodule::C_Item *item) {
            if (_projection.ManagesNativeItem(item) && IsRecordableClass(*item) && std::find(wanted.begin(), wanted.end(), item->GetId()) == wanted.end()) {
                stale.push_back(item->GetId());
            }
        });
        // The raw armor helper's default policy removes the item from the
        // inventory, including during nested slot notifications. A listener
        // can then resolve a deleted item and crash. The player's funnel
        // retains ownership and handles both armor and weapon slots.
        // Resolve each id again: an earlier unequip can also affect its peers.
        for (const auto id : stale) {
            auto *item = runtime.inventory->FindOwnedItemById(id);
            if (item != nullptr && IsWornBy(*manager, item)) {
                manager->InvokeUnequipPlayersItem(SDK::wh::entitymodule::CreateItemReferenceDescriptor(item));
            }
        }
        // Containers establish quick-access capacity before weapons are assigned.
        std::stable_partition(wanted.begin(), wanted.end(), [&](const auto id) {
            const auto *item = runtime.inventory->FindOwnedItemById(id);
            if (item == nullptr) {
                return false;
            }
            const auto itemClass = item->InvokeGetItemClassRaw();
            const auto *entry    = Shared::Game::FindItemClass(Shared::Game::CryGuidToString(itemClass.m_low, itemClass.m_high));
            return entry != nullptr && entry->category == "QuickSlotContainer";
        });
        // A piece the game took off after a pass put it on is not put back: the first pass decides.
        for (const auto id : _wornAfterDress) {
            auto *item = runtime.inventory->FindOwnedItemById(id);
            if (item != nullptr && !IsWornBy(*manager, item)) {
                _contested.insert(id);
            }
        }
        _wornAfterDress.clear();
        std::size_t dressed = 0;
        std::string refused;
        for (const auto id : wanted) {
            auto *item = runtime.inventory->FindOwnedItemById(id);
            if (item == nullptr) {
                continue;
            }
            // The player's own funnel, not the raw manager call: the raw equip
            // fills the manager but leaves the weapon sets, hands and the
            // inventory screen as if nothing were worn.
            if (!IsWornBy(*manager, item) && !_contested.contains(id)) {
                manager->InvokeEquipPlayersItem(SDK::wh::entitymodule::CreateItemReferenceDescriptor(item));
            }
            if (IsWornBy(*manager, item)) {
                ++dressed;
                _wornAfterDress.insert(id);
            }
            else {
                const auto itemClass = item->InvokeGetItemClassRaw();
                const auto *entry    = Shared::Game::FindItemClass(Shared::Game::CryGuidToString(itemClass.m_low, itemClass.m_high));
                refused += (refused.empty() ? "" : ", ") + (entry != nullptr ? std::string(entry->name) : Shared::Game::CryGuidToString(itemClass.m_low, itemClass.m_high));
            }
        }
        RestoreQuickslots(wanted);
        Framework::Logging::GetLogger(kLogger)->info("Dressed {} of {} recorded equipment item(s); awaiting verification{}{}", dressed, _equipment.Desired().size(), refused.empty() ? "" : "; the game declined ", refused);
    }

    void PlayerInventoryService::ReconcileEquipment() {
        if (!_projected || _nativeDirty.load() || _awaitingFullSync || _projectionPaused || _revision < _projectionHold || _revision < _consumeHold) {
            return;
        }
        const auto now = NowMs();
        if (!_equipment.NeedsPoll(now, _equipmentDirty.load())) {
            return;
        }
        const InventoryRuntime runtime = ResolveLocalRuntime();
        auto *manager                  = runtime.runtime != nullptr ? runtime.runtime->GetEquipmentManager() : nullptr;
        if (manager == nullptr || runtime.inventory == nullptr) {
            return;
        }
        // Consume only events preceding this observation. A worker changing
        // equipment during the check must leave its notification armed.
        const bool changed = _equipmentDirty.exchange(false);
        switch (_equipment.Poll(now, RecordedEquipment(), RecordedQuickslots())) {
        case KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress: DressFromServer(); break;
        case KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete:
            _equipmentDirty = true;
            Framework::Logging::GetLogger(kLogger)->info("Verified {} recorded equipment item(s)", _equipment.Desired().size());
            break;
        case KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Failed:
            Framework::Logging::GetLogger(kLogger)->warn("The body did not take the recorded outfit after four attempts; what it wears is reported as the outfit");
            _equipmentDirty = true;
            break;
        case KCDC::Features::Items::InventoryEquipmentReconciliation::Action::None:
            if (changed && _equipment.CanReport()) {
                _equipmentDirty = true;
            }
            break;
        }
    }

    bool PlayerInventoryService::CaptureItemState(SDK::wh::entitymodule::C_Item *nativeItem, Shared::RPC::InventoryItem &state) {
        if (nativeItem == nullptr || nativeItem->GetAmount() == 0) {
            return false;
        }

        const std::uint64_t wuid  = nativeItem->GetId();
        auto [identity, inserted] = _localItemIds.try_emplace(wuid, _nextLocalItemId);
        if (inserted) {
            ++_nextLocalItemId;
        }

        state                 = {};
        state.inventoryItemId = identity->second;
        state.itemClass       = ToWireClass(nativeItem->InvokeGetItemClassRaw());
        state.amount          = nativeItem->GetAmount();
        state.quality         = nativeItem->InvokeGetQualityRaw();
        state.condition       = nativeItem->InvokeGetConditionRaw();
        state.health          = -1.0F;
        state.phase           = -1;
        state.equipped        = false;
        state.equipmentSlot   = Shared::RPC::kNoEquipmentSlot;

        if (Shared::Game::FindMissileClass(state.itemClass) != nullptr) {
            // Native mutation takes absolute health, not displayed condition.
            state.health    = nativeItem->GetHealth();
            state.condition = -1.0F;

            auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(nativeItem->GetRuntimeData());

            if (runtime != nullptr) {
                const auto &buff = runtime->GetPoisonBuff();

                state.poisonBuff    = {buff.hipart, buff.lopart};
                state.poisonCharges = runtime->GetPoisonCharges();
            }
        }

        std::string error;
        if (!KCDC::Features::Items::ReadNativeItemEffects(*nativeItem, state.onEquipBuffs, error)) {
            return false;
        }
        KCDC::Features::Items::CustomItemModule::Get().Read(*nativeItem, state);
        return state.IsSane();
    }

    bool PlayerInventoryService::FindLocalItemClass(std::uint64_t itemWuid, Shared::RPC::ItemClassId &itemClass) {
        const InventoryRuntime runtime = ResolveLocalRuntime();
        if (runtime.inventory == nullptr || itemWuid == 0) {
            return false;
        }

        auto *item = runtime.inventory->FindOwnedItemById(itemWuid);
        if (item == nullptr) {
            return false;
        }
        itemClass = ToWireClass(item->InvokeGetItemClassRaw());
        return itemClass.IsValid();
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        using SetHandItem_t          = void(__fastcall *)(SDK::wh::entitymodule::C_ItemAttachmentManager *, std::uint32_t, std::uint64_t);
        using DropInventoryItem_t    = bool(__fastcall *)(SDK::wh::entitymodule::C_Human *, std::uint64_t, std::uint32_t, std::uint64_t, SDK::wh::entitymodule::C_Item **);
        using DropItemsUpToAmount_t                         = std::uint64_t(__fastcall *)(void *, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &, std::uint32_t);
        DropItemsUpToAmount_t g_dropItemsUpToAmountOriginal = nullptr;
        using StartPickup_t          = bool(__fastcall *)(SDK::wh::entitymodule::C_ItemPickUpHelper *, SDK::wh::entitymodule::S_ItemTransferDescriptor *);
        using CancelPickup_t         = void(__fastcall *)(SDK::wh::entitymodule::C_ItemPickUpHelper *);
        using BeginItemTransfer_t    = std::uint64_t(__fastcall *)(SDK::wh::entitymodule::C_ItemAttachmentManager *, SDK::wh::entitymodule::S_ItemTransferDescriptor *);
        using CompleteItemTransfer_t = bool(__fastcall *)(SDK::wh::entitymodule::C_ItemAttachmentManager *, SDK::wh::entitymodule::S_ItemTransferDescriptor *, bool);
        using EquipItem_t            = bool(__fastcall *)(SDK::wh::entitymodule::C_EquipmentManager *, SDK::wh::entitymodule::C_Item *, bool);
        using UnequipItem_t          = void(__fastcall *)(SDK::wh::entitymodule::C_EquipmentManager *, SDK::wh::entitymodule::C_Item *, std::uint32_t, std::uint64_t, bool);

        using SetWeaponSlotItem_t                           = void(__fastcall *)(SDK::wh::entitymodule::C_EquipmentManager *, SDK::wh::entitymodule::C_Item *);
        using ClearWeaponSlotItem_t                         = void(__fastcall *)(SDK::wh::entitymodule::C_EquipmentManager *, SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_EquipmentManager::E_EquipPolicy, SDK::wh::framework::WUID);
        SetWeaponSlotItem_t g_setWeaponSlotItemOriginal     = nullptr;
        ClearWeaponSlotItem_t g_clearWeaponSlotItemOriginal = nullptr;

        EquipItem_t g_equipItemOriginal     = nullptr;
        UnequipItem_t g_unequipItemOriginal = nullptr;

        // Armor and weapon slots have separate native mutation paths.
        // In particular, unequipping a weapon does not call UnequipItemRaw.
        bool __fastcall EquipItem_Hook(SDK::wh::entitymodule::C_EquipmentManager *manager, SDK::wh::entitymodule::C_Item *item, bool notify) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeEquipmentChanged(item);
            }
            return g_equipItemOriginal(manager, item, notify);
        }

        // All five of the native's arguments, forwarded as they came. The
        // reason and blame decide where the unequipped item goes
        // (`ApplyUnequipDestination`), so a detour that dropped them handed
        // every unequip in the game a squashed reason and a garbage WUID.
        void __fastcall UnequipItem_Hook(SDK::wh::entitymodule::C_EquipmentManager *manager, SDK::wh::entitymodule::C_Item *item, std::uint32_t reason, std::uint64_t blame, bool notify) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeEquipmentChanged(item);
            }
            g_unequipItemOriginal(manager, item, reason, blame, notify);
        }
        void __fastcall SetWeaponSlotItem_Hook(SDK::wh::entitymodule::C_EquipmentManager *manager, SDK::wh::entitymodule::C_Item *item) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeEquipmentChanged(item);
            }
            g_setWeaponSlotItemOriginal(manager, item);
        }

        void __fastcall ClearWeaponSlotItem_Hook(SDK::wh::entitymodule::C_EquipmentManager *manager, SDK::wh::entitymodule::C_Item *item, SDK::wh::entitymodule::C_EquipmentManager::E_EquipPolicy policy, SDK::wh::framework::WUID destination) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeEquipmentChanged(item);
            }
            g_clearWeaponSlotItemOriginal(manager, item, policy, destination);
        }

        using DrawOrHolsterItem_t = void *(__fastcall *)(SDK::wh::entitymodule::C_Item **, SDK::wh::entitymodule::S_ActionRequestResult *, SDK::wh::entitymodule::C_Item *, std::int32_t, SDK::wh::entitymodule::C_Actor *, bool, void *);

        SetHandItem_t g_setHandItemOriginal                   = nullptr;
        DropInventoryItem_t g_dropInventoryItemOriginal       = nullptr;
        StartPickup_t g_startPickupOriginal                   = nullptr;
        CancelPickup_t g_cancelPickupOriginal                 = nullptr;
        BeginItemTransfer_t g_beginItemTransferOriginal       = nullptr;
        CompleteItemTransfer_t g_completeItemTransferOriginal = nullptr;
        DrawOrHolsterItem_t g_drawOrHolsterItemOriginal       = nullptr;

        using HolderAddItem_t                                 = SDK::wh::entitymodule::C_Item *(__fastcall *)(SDK::wh::entitymodule::C_ItemHolder *, SDK::wh::entitymodule::C_Item *, bool);
        using TransferItemToHolder_t                          = SDK::wh::entitymodule::C_Item *(__fastcall *)(SDK::wh::entitymodule::C_ItemHolder *, SDK::wh::entitymodule::C_Item *, bool);
        using RemoveItem_t                                    = void(__fastcall *)(SDK::wh::entitymodule::C_ItemHolder *, SDK::wh::entitymodule::C_Item *, std::uint32_t, std::uint32_t);
        using AdjustAmount_t                                  = bool(__fastcall *)(SDK::wh::entitymodule::C_Item *, std::int32_t);
        HolderAddItem_t g_holderAddItemOriginal               = nullptr;
        TransferItemToHolder_t g_transferItemToHolderOriginal = nullptr;
        RemoveItem_t g_removeItemOriginal                     = nullptr;
        AdjustAmount_t g_adjustAmountOriginal                 = nullptr;

        /**
         * The game adding to, moving out of or taking from the local inventory
         * on its own. There is no one function for it: items are created
         * through `C_ItemHolder::AddItem`, moved through `C_Inventory::AddItem`
         * (the missile hooks) and `TransferItemToHolder`, and taken through
         * `RemoveItem` and `AdjustAmount`. Their callers expect success, so
         * none is refused; each only asks for the next projection, which puts
         * the server's rows back.
         */
        SDK::wh::entitymodule::C_Item *__fastcall HolderAddItem_Hook(SDK::wh::entitymodule::C_ItemHolder *holder, SDK::wh::entitymodule::C_Item *item, bool allowMerge) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeInventoryChanged(holder, item);
            }
            return g_holderAddItemOriginal(holder, item, allowMerge);
        }

        SDK::wh::entitymodule::C_Item *__fastcall TransferItemToHolder_Hook(SDK::wh::entitymodule::C_ItemHolder *holder, SDK::wh::entitymodule::C_Item *item, bool preventMerge) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeInventoryChanged(holder, item);
            }
            return g_transferItemToHolderOriginal(holder, item, preventMerge);
        }

        void __fastcall RemoveItem_Hook(SDK::wh::entitymodule::C_ItemHolder *holder, SDK::wh::entitymodule::C_Item *item, std::uint32_t reason, std::uint32_t count) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeInventoryChanged(holder, item);
            }
            g_removeItemOriginal(holder, item, reason, count);
        }

        bool __fastcall AdjustAmount_Hook(SDK::wh::entitymodule::C_Item *item, std::int32_t delta) {
            if (KCDC::Core::gApplication != nullptr && delta != 0) {
                KCDC::Core::gApplication->GetLocalEvents().NativeInventoryChanged(nullptr, item);
            }
            return g_adjustAmountOriginal(item, delta);
        }

        SDK::wh::entitymodule::C_Human *LocalHumanActor() {
            auto *framework = SDK::IGameFramework::GetInstance();
            return static_cast<SDK::wh::entitymodule::C_Human *>(framework != nullptr ? framework->GetClientActor() : nullptr);
        }

        bool IsLocalAttachmentManager(SDK::wh::entitymodule::C_ItemAttachmentManager *manager) {
            auto *localHuman = LocalHumanActor();
            return localHuman != nullptr && manager != nullptr && manager == localHuman->GetItemAttachmentManager();
        }

        /**
         * The local body put an item into a hand, or took one out.
         *
         * The only writer of the actor's hand-item state, and the one signal
         * that means an item is really out -- the equipment weapon slot fills
         * in from equipping alone. Both hands are followed; the draw and the
         * holster edge both reach it, so one detour sees every transition.
         *
         * Weapon draws report earlier, from the decision function below; this
         * still covers the torch, drops and anything scripted.
         */
        void __fastcall SetHandItem_Hook(SDK::wh::entitymodule::C_ItemAttachmentManager *manager, std::uint32_t hand, std::uint64_t itemWuid) {
            g_setHandItemOriginal(manager, hand, itemWuid);

            if (KCDC::Core::gApplication == nullptr || !IsLocalAttachmentManager(manager)) {
                return;
            }

            const std::uint64_t empty = SDK::wh::entitymodule::C_ItemAttachmentManager::GetInvalidItemId().value;
            KCDC::Core::gApplication->GetLocalEvents().HeldItemChanged(hand, itemWuid == empty ? 0 : itemWuid);
        }

        /**
         * The native selection iterator keeps its permission checks and exact
         * quantities. Its intercepted stack callbacks collect one request;
         * forward its return register unchanged after finishing the selection.
         */
        std::uint64_t __fastcall DropItemsUpToAmount_Hook(void *context, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount) {
            if (KCDC::Core::gApplication == nullptr) {
                return g_dropItemsUpToAmountOriginal(context, items, amount);
            }
            auto &events = KCDC::Core::gApplication->GetLocalEvents();
            events.InventoryDropSelectionStarting();
            const auto result = g_dropItemsUpToAmountOriginal(context, items, amount);
            events.InventoryDropSelectionFinished();
            return result;
        }

        /** A native drop moved one of the local player's items into the world. */
        bool __fastcall DropInventoryItem_Hook(SDK::wh::entitymodule::C_Human *human, std::uint64_t itemWuid, std::uint32_t amount, std::uint64_t context, SDK::wh::entitymodule::C_Item **droppedItem) {
            // Reported before as well as after: whether the item was in a hand
            // is only true until the drop runs.
            const bool local = KCDC::Core::gApplication != nullptr && human != nullptr && human == LocalHumanActor();
            if (local && !KCDC::Core::gApplication->GetPlayerInventoryService().IsApplyingAuthority() && KCDC::Core::gApplication->GetLocalEvents().RequestInventoryDrop(itemWuid, amount)) {
                if (droppedItem != nullptr) {
                    *droppedItem = nullptr;
                }
                return false;
            }
            if (local) {
                KCDC::Core::gApplication->GetLocalEvents().InventoryDropStarting(itemWuid);
            }

            const bool succeeded = g_dropInventoryItemOriginal(human, itemWuid, amount, context, droppedItem);
            if (local && succeeded && droppedItem != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().InventoryItemDropped(itemWuid, *droppedItem);
            }
            return succeeded;
        }

        bool __fastcall StartPickup_Hook(SDK::wh::entitymodule::C_ItemPickUpHelper *helper, SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) {
            if (KCDC::Core::gApplication != nullptr && helper != nullptr && helper->GetHuman() == LocalHumanActor() && KCDC::Core::gApplication->GetGroundItemService().RequestCarryInstead(descriptor)) {
                return false;
            }
            if (KCDC::Core::gApplication != nullptr && helper != nullptr && helper->GetHuman() == LocalHumanActor() && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority() && !KCDC::Core::gApplication->GetPlayerInventoryService().IsApplyingAuthority()) {
                KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, true);
                return false;
            }
            const bool tracked =
                KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority() && helper != nullptr && helper->GetHuman() == LocalHumanActor() && KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, true);
            const bool accepted = g_startPickupOriginal(helper, descriptor);
            if (tracked && !accepted) {
                KCDC::Core::gApplication->GetLocalEvents().PickupCancelled();
            }
            return accepted;
        }

        std::uint64_t __fastcall BeginItemTransfer_Hook(SDK::wh::entitymodule::C_ItemAttachmentManager *manager, SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) {
            if (KCDC::Core::gApplication != nullptr && IsLocalAttachmentManager(manager) && KCDC::Core::gApplication->GetGroundItemService().RequestCarryInstead(descriptor)) {
                return 0;
            }
            if (KCDC::Core::gApplication != nullptr && IsLocalAttachmentManager(manager) && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority() && !KCDC::Core::gApplication->GetPlayerInventoryService().IsApplyingAuthority()) {
                KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, false);
                return 0;
            }
            const bool tracked         = KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority() && IsLocalAttachmentManager(manager) && KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, false);
            const std::uint64_t result = g_beginItemTransferOriginal(manager, descriptor);
            if (tracked) {
                // The direct transfer path can move the item synchronously;
                // the animated one is settled by the completion hook instead.
                KCDC::Core::gApplication->GetLocalEvents().PickupSettled(result != 0);
            }
            return result;
        }

        bool __fastcall CompleteItemTransfer_Hook(SDK::wh::entitymodule::C_ItemAttachmentManager *manager, SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor, bool forceCompletion) {
            // An item carry's pick-up moves the item into the carry action's
            // own holder, never into the bags, so it is no inventory change
            // for the authority to refuse.
            if (KCDC::Core::gApplication != nullptr && descriptor != nullptr && KCDC::Core::gApplication->GetPlayerService().IsItemCarryHolder(descriptor->GetDestinationHolder())) {
                return g_completeItemTransferOriginal(manager, descriptor, forceCompletion);
            }
            const bool isLocal = KCDC::Core::gApplication != nullptr && IsLocalAttachmentManager(manager);
            if (isLocal && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority() && !KCDC::Core::gApplication->GetPlayerInventoryService().IsApplyingAuthority()) {
                KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, false);
                return false;
            }
            if (isLocal && !KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
                KCDC::Core::gApplication->GetLocalEvents().PickupStarting(descriptor, false);
            }

            const bool tracked   = isLocal && KCDC::Core::gApplication->GetLocalEvents().IsPickupTrackedFor(descriptor);
            const bool succeeded = g_completeItemTransferOriginal(manager, descriptor, forceCompletion);
            if (tracked) {
                KCDC::Core::gApplication->GetLocalEvents().PickupSettled(succeeded);
            }
            return succeeded;
        }

        void __fastcall CancelPickup_Hook(SDK::wh::entitymodule::C_ItemPickUpHelper *helper) {
            g_cancelPickupOriginal(helper);
            if (KCDC::Core::gApplication != nullptr && helper != nullptr && helper->GetHuman() == LocalHumanActor()) {
                KCDC::Core::gApplication->GetLocalEvents().PickupCancelled();
            }
        }
    } // namespace

    /**
     * The local body committed to drawing or putting away a weapon set.
     *
     * The start edge. `SetHandItem` fires from the draw animation's own
     * `Attach` event, near the end of the transition, so a body driven
     * from it starts its draw an animation late. This is where the owner
     * decides, on the path the draw key takes (`C_QAMManager`).
     *
     * `IsItemDrawn` is read before the original runs -- the same
     * predicate the callee uses to pick its own branch.
     */
    void *__fastcall DrawOrHolsterItem_Hook(SDK::wh::entitymodule::C_Item **pair, SDK::wh::entitymodule::S_ActionRequestResult *out, SDK::wh::entitymodule::C_Item *mainItem, std::int32_t qamSlot, SDK::wh::entitymodule::C_Actor *actor, bool animated, void *callback) {
        const bool local      = actor != nullptr && actor == static_cast<SDK::wh::entitymodule::C_Actor *>(LocalHumanActor());
        const bool holstering = local && mainItem != nullptr && SDK::wh::entitymodule::IsItemDrawn(actor, SDK::wh::framework::WUID {mainItem->GetId()});

        void *result = g_drawOrHolsterItemOriginal(pair, out, mainItem, qamSlot, actor, animated, callback);

        // A refused transition never plays, so it is not reported.
        if (KCDC::Core::gApplication == nullptr || !local || !animated || out == nullptr || !out->Accepted()) {
            return result;
        }

        // The engine's own filler names the hands the owner's draw fills.
        std::uint64_t handItems[SDK::wh::entitymodule::S_ActorAnimDrawRequest::kHandCount] {};
        SDK::wh::entitymodule::BuildDrawHandPair(pair, handItems);

        const std::uint64_t empty = SDK::wh::entitymodule::C_ItemAttachmentManager::GetInvalidItemId().value;
        for (std::uint32_t hand = 0; hand < SDK::wh::entitymodule::S_ActorAnimDrawRequest::kHandCount; ++hand) {
            if (handItems[hand] == empty) {
                continue;
            }
            KCDC::Core::gApplication->GetLocalEvents().HeldItemChanged(hand, holstering ? 0 : handItems[hand]);
        }
        return result;
    }

} // namespace KCDC::Features::Player

static InitFunction initInventoryHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemAttachmentManager__SetHandItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetHandItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setHandItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Human__DropInventoryItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DropInventoryItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_dropInventoryItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.WH__DropItemsUpToAmount), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DropItemsUpToAmount_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_dropItemsUpToAmountOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemPickUpHelper__StartPickup), reinterpret_cast<LPVOID>(&KCDC::Features::Player::StartPickup_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_startPickupOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemPickUpHelper__CancelPickup), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CancelPickup_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_cancelPickupOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemAttachmentManager__BeginItemTransfer), reinterpret_cast<LPVOID>(&KCDC::Features::Player::BeginItemTransfer_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_beginItemTransferOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemAttachmentManager__CompleteItemTransfer), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CompleteItemTransfer_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_completeItemTransferOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.WH__DrawOrHolsterItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DrawOrHolsterItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_drawOrHolsterItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_EquipmentManager__EquipItemRaw), reinterpret_cast<LPVOID>(&KCDC::Features::Player::EquipItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_equipItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemHolder__AddItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::HolderAddItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_holderAddItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemHolder__TransferItemToHolder), reinterpret_cast<LPVOID>(&KCDC::Features::Player::TransferItemToHolder_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_transferItemToHolderOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ItemHolder__RemoveItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RemoveItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_removeItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Item__AdjustAmount), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AdjustAmount_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_adjustAmountOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_EquipmentManager__SetWeaponSlotItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetWeaponSlotItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setWeaponSlotItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_EquipmentManager__ClearWeaponSlotItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ClearWeaponSlotItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_clearWeaponSlotItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_EquipmentManager__UnequipItemRaw), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UnequipItem_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_unequipItemOriginal));
    },
    "Items");

namespace KCDC::Features::Player {
    namespace {
        // Shipped item_category.xml values. The native enum's namespace is
        // unproven, so these policy constants belong to the mod.
        constexpr std::int32_t kFoodItemCategory     = 5;
        constexpr std::int32_t kOintmentItemCategory = 13;

        // C_Actor vtable slot 195: RCX actor, RDX result word, R8 item, R9d
        // amount, then context (-1 sentinel) and the food check flag.
        using UseItem_t             = KCDC::Features::Items::NativeItemUseResultWord *(__fastcall *)(SDK::wh::entitymodule::C_Actor *, KCDC::Features::Items::NativeItemUseResultWord *, SDK::wh::entitymodule::C_Item *, std::int32_t, std::int32_t, bool);
        UseItem_t g_useItemOriginal = nullptr;

        using UseConsumableItem_t                       = void(__fastcall *)(SDK::wh::guimodule::C_UIApseInventory *, SDK::wh::entitymodule::C_Item *, bool);
        UseConsumableItem_t g_useConsumableItemOriginal = nullptr;
        void __fastcall UseConsumableItem(SDK::wh::guimodule::C_UIApseInventory *ui, SDK::wh::entitymodule::C_Item *item, bool saveConfirmed) {
            if (KCDC::Core::gApplication != nullptr && item != nullptr && static_cast<std::int32_t>(item->InvokeGetCategoryRaw()) == kFoodItemCategory && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
                const KCDC::Features::Items::NativeFoodUse use {1, -1, false, KCDC::Features::Items::NativeFoodUse::Source::Inventory, saveConfirmed};
                if (KCDC::Core::gApplication->GetLocalEvents().InterceptFoodUse(item, use)) {
                    return;
                }
            }
            g_useConsumableItemOriginal(ui, item, saveConfirmed);
        }

        using ApplyFood_t               = void(__fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *, SDK::wh::rpgmodule::C_Soul *, SDK::wh::entitymodule::C_Item *);
        ApplyFood_t g_applyFoodOriginal = nullptr;
        void __fastcall ApplyFood(SDK::wh::rpgmodule::C_RPGInventory *rules, SDK::wh::rpgmodule::C_Soul *soul, SDK::wh::entitymodule::C_Item *item) {
            auto *framework = SDK::IGameFramework::GetInstance();
            auto *actor     = static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
            if (KCDC::Core::gApplication != nullptr && actor != nullptr && actor->GetSoul() == soul && !KCDC::Core::gApplication->GetLocalEvents().AllowsFoodEffects()) {
                return;
            }
            g_applyFoodOriginal(rules, soul, item);
        }

        using CalculateItemDecay_t                        = float(__fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *, SDK::wh::entitymodule::C_Item *, float, SDK::wh::rpgmodule::I_Soul *);
        CalculateItemDecay_t g_calculateItemDecayOriginal = nullptr;

        // C_Item::CanMergeStackWith: RCX source, RDX candidate, R8 expected
        // owner, R9b source-flag permission, then two stack booleans.
        using CanMergeStackWith_t                       = bool(__fastcall *)(SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_ItemHolder *, bool, bool, bool);
        CanMergeStackWith_t g_canMergeStackWithOriginal = nullptr;

        // The two writers every item health and quality change ends in; the
        // last argument batches holder notifications.
        using SetHealthCore_t                     = void(__fastcall *)(SDK::wh::entitymodule::C_Item *, float, void *);
        using SetQualityCore_t                    = void(__fastcall *)(SDK::wh::entitymodule::C_Item *, std::uint32_t, void *);
        SetHealthCore_t g_setHealthCoreOriginal   = nullptr;
        SetQualityCore_t g_setQualityCoreOriginal = nullptr;

        // C_RPGInventory vtable slot 23: RCX rules, RDX soul, R8 kit, R9
        // equipment. Its one caller is the repair-kit combiners' shared slot 3
        // (0x182A456E4), which CombineItems repeats until the equipment is
        // whole -- so the original always runs.
        using ApplyRepairKit_t                    = void(__fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *, SDK::wh::rpgmodule::C_Soul *, SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_Item *);
        ApplyRepairKit_t g_applyRepairKitOriginal = nullptr;

        void __fastcall ApplyRepairKit(SDK::wh::rpgmodule::C_RPGInventory *rules, SDK::wh::rpgmodule::C_Soul *soul, SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeRepairStarting(kit, equipment);
            }
            g_applyRepairKitOriginal(rules, soul, kit, equipment);
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().NativeRepairFinished(kit, equipment);
            }
        }

        // C_RPGInventory::PrepareItems: RCX rules, RDX the hidden result -- the
        // WUIDs of the items made, one per converted input in input order,
        // stopping at the first that fails -- R8 soul, R9 the inputs, then the
        // effect factory (a std::function passed through untouched), the unit
        // count (negative for every unit) and the preparation.
        using PrepareItems_t = SDK::StdVector<SDK::wh::framework::WUID> *(
            __fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *, SDK::StdVector<SDK::wh::framework::WUID> *, SDK::wh::rpgmodule::I_Soul *, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> *, const void *, std::int32_t, SDK::wh::entitymodule::E_PrepareFood::Type);
        PrepareItems_t g_prepareItemsOriginal = nullptr;

        // A drying rack, a smokehouse or a fire converts what the player put
        // to it the moment its animation ends; the game makes the dried,
        // smoked or cooked stack and removes the original here, and the server
        // is told what it took.
        SDK::StdVector<SDK::wh::framework::WUID> *__fastcall PrepareItems(SDK::wh::rpgmodule::C_RPGInventory *rules, SDK::StdVector<SDK::wh::framework::WUID> *prepared, SDK::wh::rpgmodule::I_Soul *soul, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> *items,
            const void *createEffect, std::int32_t count, SDK::wh::entitymodule::E_PrepareFood::Type preparation) {
            auto *framework = SDK::IGameFramework::GetInstance();
            auto *actor     = static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
            if (KCDC::Core::gApplication == nullptr || actor == nullptr || actor->GetSoul() != soul) {
                return g_prepareItemsOriginal(rules, prepared, soul, items, createEffect, count, preparation);
            }
            // Read before the original: it removes every stack it converts.
            std::vector<std::uint64_t> stacks;
            for (auto *item : *items) {
                stacks.push_back(item->GetId());
            }
            // Its survival XP is the server's to grant with the conversion.
            auto &progression = KCDC::Core::gApplication->GetPlayerProgressionService();
            progression.SetServerGrantsGains(true);
            auto *result = g_prepareItemsOriginal(rules, prepared, soul, items, createEffect, count, preparation);
            progression.SetServerGrantsGains(false);
            if (!result->IsEmpty()) {
                stacks.resize(result->Size());
                KCDC::Core::gApplication->GetLocalEvents().NativeItemsPrepared(preparation, stacks, count);
            }
            return result;
        }

        bool __fastcall CanMergeStackWith(SDK::wh::entitymodule::C_Item *source, SDK::wh::entitymodule::C_Item *candidate, SDK::wh::entitymodule::C_ItemHolder *expectedOwner, bool allowSourceFlag, bool ignorePhysicalHolder, bool expectedStatusFlag) {
            if (KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetLocalEvents().AllowNativeItemMerge(source, candidate)) {
                return false;
            }
            return g_canMergeStackWithOriginal(source, candidate, expectedOwner, allowSourceFlag, ignorePhysicalHolder, expectedStatusFlag);
        }

        float __fastcall CalculateItemDecay(SDK::wh::rpgmodule::C_RPGInventory *rules, SDK::wh::entitymodule::C_Item *item, float elapsedSeconds, SDK::wh::rpgmodule::I_Soul *ownerSoul) {
            if (KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetLocalEvents().AllowNativeItemDecay(item)) {
                return 0.0F;
            }
            return g_calculateItemDecayOriginal(rules, item, elapsedSeconds, ownerSoul);
        }

        // Combat wear runs on the combat workers with the engine lock held:
        // these only record the item, and the game thread reads it later.
        void __fastcall SetHealthCore(SDK::wh::entitymodule::C_Item *item, float health, void *batch) {
            g_setHealthCoreOriginal(item, health, batch);
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerInventoryService().ManagesNativeItem(item)) {
                KCDC::Core::gApplication->GetPlayerInventoryService().NotifyNativeWear(item->GetId());
            }
        }

        void __fastcall SetQualityCore(SDK::wh::entitymodule::C_Item *item, std::uint32_t quality, void *batch) {
            g_setQualityCoreOriginal(item, quality, batch);
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerInventoryService().ManagesNativeItem(item)) {
                KCDC::Core::gApplication->GetPlayerInventoryService().NotifyNativeWear(item->GetId());
            }
        }

        KCDC::Features::Items::NativeItemUseResultWord *__fastcall UseItem(SDK::wh::entitymodule::C_Actor *actor, KCDC::Features::Items::NativeItemUseResultWord *result, SDK::wh::entitymodule::C_Item *item, std::int32_t amount, std::int32_t context, bool checkFood) {
            if (KCDC::Core::gApplication != nullptr && item != nullptr) {
                auto *framework     = SDK::IGameFramework::GetInstance();
                const bool local    = framework != nullptr && actor == framework->GetClientActor();
                const auto category = static_cast<std::int32_t>(item->InvokeGetCategoryRaw());
                // item_category.xml: 5 food (including potions), 13 ointment.
                // Category 26 is equippable and shares this entry; leave it
                // to the native equipment path, which does not consume stock.
                auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
                if (local && category == kFoodItemCategory && inventory.HasAuthority()) {
                    if (KCDC::Core::gApplication->GetLocalEvents().InterceptFoodUse(item, {amount, context, checkFood})) {
                        result->RefuseUnsupported();
                        return result;
                    }
                    auto *answer = g_useItemOriginal(actor, result, item, amount, context, checkFood);
                    KCDC::Core::gApplication->GetLocalEvents().FoodUseFinished((answer->flags & 0x1FEU) == 0);
                    return answer;
                }
                if (local && (category == kFoodItemCategory || category == kOintmentItemCategory) && inventory.HasAuthority()) {
                    // The game eats first; the spent unit is reported next frame.
                    // Only a stack the server projected can be spent.
                    if (!inventory.CanSpend() || !inventory.ManagesNativeItem(item)) {
                        inventory.RefuseUnsupportedAction("Your inventory is still synchronizing with the server.");
                        result->RefuseUnsupported();
                        return result;
                    }
                    inventory.ArmConsumption(KCDC::Features::Player::PlayerInventoryService::ConsumptionKind::Use, item->GetId());
                }
            }
            return g_useItemOriginal(actor, result, item, amount, context, checkFood);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initInventoryActionHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseInventory__UseConsumableItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UseConsumableItem), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_useConsumableItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__ApplyFoodConsumption), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ApplyFood), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_applyFoodOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__UseItem), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UseItem), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_useItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__CalculateItemDecayRaw), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CalculateItemDecay), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_calculateItemDecayOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Item__CanMergeStackWith), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CanMergeStackWith), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_canMergeStackWithOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Item__SetHealthCore), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetHealthCore), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setHealthCoreOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Item__SetQualityCore), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetQualityCore), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setQualityCoreOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__ApplyRepairKit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ApplyRepairKit), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_applyRepairKitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__PrepareItems), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PrepareItems), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_prepareItemsOriginal));
    },
    "Item actions");
