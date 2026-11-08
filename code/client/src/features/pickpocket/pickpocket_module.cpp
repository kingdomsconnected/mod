#include "features/pickpocket/pickpocket_module.h"

#include "core/application.h"
#include "features/player/player.h"
#include "features/player/native_player_module.h"
#include "features/ui/ui_hud_notifications.h"
#include "common/module_support.h"

#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <iterator>

namespace KCDC::Features::Pickpocket {
    namespace {
        constexpr const char *kLogger = "Pickpocket";

        /**
         * How long the charge may wait on the server. The native charge takes
         * a few seconds on its own, so a verdict normally lands long before
         * the grid would open; this only ends an attempt nobody answers.
         */
        constexpr MafiaNet::Time kVerdictTimeoutMs = 6000;

        Shared::RPC::ItemClassId ClassOf(const SDK::wh::entitymodule::C_Item *item) {
            const SDK::wh::entitymodule::S_ItemClassGUID itemClass = item->InvokeGetItemClassRaw();
            return Shared::RPC::ItemClassId {itemClass.m_low, itemClass.m_high};
        }

        /** Adds `amount` of `itemClass` to `lines`, one line per class, each held to what one give or take may carry. */
        void Accumulate(std::vector<Shared::RPC::PickpocketItem> &lines, const Shared::RPC::ItemClassId &itemClass, std::uint32_t amount) {
            auto line = std::find_if(lines.begin(), lines.end(), [&itemClass](const Shared::RPC::PickpocketItem &candidate) {
                return candidate.itemClass == itemClass;
            });
            if (line == lines.end()) {
                lines.push_back(Shared::RPC::PickpocketItem {itemClass, 0});
                line = std::prev(lines.end());
            }
            line->amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(line->amount) + amount, Shared::RPC::kMaxGiveAmount));
        }

        /** The remote player whose body this is, or null for the local player's body, an NPC or nothing. */
        KCDC::Features::Player::Human *FindRemoteBody(SDK::EntityId entityId) {
            if (entityId == 0) {
                return nullptr;
            }
            return KCDC::Common::FindReplica<KCDC::Features::Player::Human>([entityId](KCDC::Features::Player::Human *human) {
                return !human->IsOwner() && human->GetEntityId() == entityId;
            });
        }

        const char *Explain(Shared::RPC::PickpocketRefusal refusal) {
            switch (refusal) {
            case Shared::RPC::PickpocketRefusal::TooFar: return "You are too far away to reach their pockets.";
            case Shared::RPC::PickpocketRefusal::Refused:
            case Shared::RPC::PickpocketRefusal::Unavailable: return "You cannot pick this person's pocket right now.";
            case Shared::RPC::PickpocketRefusal::None: break;
            }
            return nullptr;
        }

        /** Still in play: charging, or with the loot grid up. */
        bool IsRunning(const SDK::wh::playermodule::C_Pickpocketing *minigame) {
            const SDK::wh::playermodule::E_PickpocketingState state = minigame->GetState();
            return state == SDK::wh::playermodule::E_PickpocketingState::Charging || state == SDK::wh::playermodule::E_PickpocketingState::Charged || state == SDK::wh::playermodule::E_PickpocketingState::Looting;
        }
    } // namespace

    PickpocketModule &PickpocketModule::Get() {
        static PickpocketModule instance;
        return instance;
    }

    void PickpocketModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }

        // The level took the victim's body and the minigame's world with it:
        // nothing native is touched, the server settles the attempt by its
        // own timeout.
        if (_nativeContextLost.exchange(false) && _phase != Phase::Idle) {
            _stocked.clear();
            Clear();
            return;
        }

        switch (_phase) {
        case Phase::Idle: break;

        case Phase::AwaitingVerdict:
            // The charge ended before the server answered -- let go, walked
            // off -- without passing the resolver.
            if (!IsRunning(_minigame)) {
                Framework::Logging::GetLogger(kLogger)->info("Attempt {} ended before the server answered", _requestId);
                SendResult(Shared::RPC::PickpocketOutcome::Abandoned, {});
                Clear();
            }
            else if (MafiaNet::GetTime() >= _deadline) {
                Framework::Logging::GetLogger(kLogger)->warn("No verdict for attempt {} in time; ending it", _requestId);
                UI::Hud::ShowNotification(Explain(Shared::RPC::PickpocketRefusal::Unavailable));
                Abandon(true);
            }
            break;

        case Phase::Stocked:
            // Ended by something that finishes without resolving -- the
            // thief's own death, a scripted stop. Nothing was taken.
            if (!IsRunning(_minigame)) {
                Framework::Logging::GetLogger(kLogger)->info("Attempt {} ended before the loot screen opened", _requestId);
                SendResult(Shared::RPC::PickpocketOutcome::Abandoned, {});
                Unstock();
                Clear();
            }
            break;

        case Phase::Resolved:
            // The loot callback resolves and then finishes in one go, but an
            // interruption resolves with the grid still up; the created items
            // wait until nothing can be showing them.
            if (_minigame->GetState() != SDK::wh::playermodule::E_PickpocketingState::Looting) {
                Unstock();
                Clear();
            }
            break;
        }
    }

    void PickpocketModule::Reset() {
        if (_phase != Phase::Idle) {
            Abandon(false);
        }
        _rpcsInstalled     = false;
        _nativeContextLost = false;
    }

    void PickpocketModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    void PickpocketModule::OnStarted(SDK::wh::playermodule::C_Pickpocketing *minigame) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || minigame == nullptr || minigame->GetActor() != KCDC::Common::LocalHumanActor()) {
            return;
        }
        KCDC::Features::Player::Human *victim = FindRemoteBody(minigame->GetVictimEntityId());
        if (victim == nullptr) {
            return;
        }

        // Whatever came before is over. `BeginInteraction` finishes an
        // interaction still open without resolving it, inside the very Start
        // this runs after -- so an attempt the thief let go of half-charged
        // ends here, unseen by the tick, and is reported and cleaned up here.
        if (_phase == Phase::AwaitingVerdict || _phase == Phase::Stocked) {
            SendResult(Shared::RPC::PickpocketOutcome::Abandoned, {});
        }
        if (_phase != Phase::Idle) {
            Unstock();
        }
        Clear();

        _requestId = _nextRequestId++;
        if (_nextRequestId == 0) {
            _nextRequestId = 1;
        }
        _minigame        = minigame;
        _victimEntityId  = minigame->GetVictimEntityId();
        _victimNetworkId = victim->GetNetworkID();
        _deadline        = MafiaNet::GetTime() + kVerdictTimeoutMs;
        _phase           = Phase::AwaitingVerdict;

        Shared::RPC::PickpocketRequest request;
        request.requestId       = _requestId;
        request.victimNetworkId = _victimNetworkId;
        net->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(kLogger)->info("Picking the pocket of '{}' (networkId {}); attempt {}", victim->nickname, _victimNetworkId, _requestId);
    }

    bool PickpocketModule::MayOpenLoot(const SDK::wh::playermodule::C_Pickpocketing *minigame) const {
        return minigame != _minigame || _phase != Phase::AwaitingVerdict;
    }

    void PickpocketModule::BeginLootGrid(const SDK::wh::playermodule::C_Pickpocketing *minigame) {
        _buildingGrid = minigame == _minigame && _phase == Phase::Stocked;
    }

    void PickpocketModule::EndLootGrid() {
        _buildingGrid = false;
    }

    bool PickpocketModule::HidesFromLootGrid(const SDK::wh::entitymodule::C_Item *item) const {
        return _buildingGrid && item != nullptr && _stocked.find(item->GetId()) == _stocked.end();
    }

    void PickpocketModule::OnResolving(const SDK::wh::playermodule::C_Pickpocketing *minigame, bool success, const SDK::StackVector<SDK::wh::framework::WUID, 11> *taken, std::uint8_t reason) {
        if (minigame != _minigame || (_phase != Phase::AwaitingVerdict && _phase != Phase::Stocked)) {
            return;
        }

        // The resolver's own reason codes: 2 is the victim, whether the charge
        // failed its threshold or the loot screen failed.
        constexpr std::uint8_t kReasonVictim   = 2;
        Shared::RPC::PickpocketOutcome outcome = Shared::RPC::PickpocketOutcome::Abandoned;
        if (success) {
            outcome = Shared::RPC::PickpocketOutcome::Stole;
        }
        else if (reason == kReasonVictim) {
            outcome = Shared::RPC::PickpocketOutcome::Caught;
        }

        // Only what this attempt created can be claimed, so a WUID the body
        // carried already -- or one the grid should never have offered -- is
        // not a line.
        std::vector<Shared::RPC::PickpocketItem> lines;
        SDK::wh::entitymodule::C_Inventory *inventory = VictimInventory();
        if (outcome == Shared::RPC::PickpocketOutcome::Stole && taken != nullptr && inventory != nullptr) {
            for (const SDK::wh::framework::WUID &itemId : *taken) {
                if (_stocked.find(itemId.value) == _stocked.end()) {
                    continue;
                }
                const SDK::wh::entitymodule::C_Item *item = inventory->FindItemById(itemId.value);
                if (item != nullptr && item->GetAmount() != 0 && lines.size() < Shared::RPC::kMaxPickpocketTaken) {
                    Accumulate(lines, ClassOf(item), item->GetAmount());
                }
            }
        }

        SendResult(outcome, lines);
        _phase = Phase::Resolved;
        Framework::Logging::GetLogger(kLogger)->info("Attempt {} resolved: outcome {}, {} line(s) claimed", _requestId, static_cast<std::uint32_t>(outcome), lines.size());
    }

    bool PickpocketModule::RefusesMessage(const SDK::wh::playermodule::C_Minigame *minigame) const {
        return minigame != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemoteEntity(minigame->GetInteractionEntityId());
    }

    bool PickpocketModule::RefusesTransfer(const SDK::wh::rpgmodule::C_StealItemsEffect *effect) const {
        return effect != nullptr && KCDC::Features::Player::Human::FindRemoteSoulWuid(effect->GetVictimSoulWuid().value) != nullptr;
    }

    void PickpocketModule::ForgetVictim(SDK::EntityId entityId) {
        if (_phase == Phase::Idle || entityId == 0 || entityId != _victimEntityId) {
            return;
        }
        if (_phase == Phase::Resolved) {
            Unstock();
            Clear();
            return;
        }
        Abandon(true);
    }

    void PickpocketModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _rpcsInstalled = true;

        net->RegisterRPC<Shared::RPC::PickpocketVerdict>([](const Shared::RPC::PickpocketVerdict &verdict, MafiaNet::Packet *) {
            if (verdict.IsSane()) {
                PickpocketModule::Get().OnVerdict(verdict);
            }
        });
    }

    void PickpocketModule::OnVerdict(const Shared::RPC::PickpocketVerdict &verdict) {
        // A verdict for an attempt already over is a late answer, not this one.
        if (_phase != Phase::AwaitingVerdict || verdict.requestId != _requestId) {
            return;
        }

        if (!verdict.Accepted()) {
            if (const char *text = Explain(static_cast<Shared::RPC::PickpocketRefusal>(verdict.refusal))) {
                UI::Hud::ShowNotification(text);
            }
            Framework::Logging::GetLogger(kLogger)->info("Attempt {} refused by the server (reason {})", _requestId, verdict.refusal);
            Abandon(false);
            return;
        }

        if (!IsRunning(_minigame) || !Stock(verdict.items)) {
            Framework::Logging::GetLogger(kLogger)->warn("Attempt {} could not be stocked; ending it", _requestId);
            Abandon(true);
            return;
        }
        _phase = Phase::Stocked;
        Framework::Logging::GetLogger(kLogger)->info("Attempt {} stocked with {} line(s)", _requestId, verdict.items.size());
    }

    bool PickpocketModule::Stock(const std::vector<Shared::RPC::PickpocketItem> &items) {
        SDK::wh::entitymodule::C_Inventory *inventory = VictimInventory();
        if (inventory == nullptr) {
            return false;
        }

        std::unordered_set<std::uint64_t> carried;
        for (const SDK::wh::entitymodule::C_Item *item : inventory->GetItems()) {
            if (item != nullptr) {
                carried.insert(item->GetId());
            }
        }

        for (const Shared::RPC::PickpocketItem &line : items) {
            inventory->CreateItems(SDK::wh::entitymodule::S_ItemClassGUID {line.itemClass.low, line.itemClass.high}, static_cast<int>(line.amount), false);
        }

        // Diffed rather than taken from `CreateItems`, whose null return does
        // not prove nothing was made (see PlayerInventoryService).
        for (const SDK::wh::entitymodule::C_Item *item : inventory->GetItems()) {
            if (item != nullptr && carried.find(item->GetId()) == carried.end()) {
                _stocked.insert(item->GetId());
            }
        }
        return true;
    }

    void PickpocketModule::Unstock() {
        SDK::wh::entitymodule::C_Inventory *inventory = VictimInventory();
        if (inventory != nullptr && !_stocked.empty()) {
            std::vector<SDK::wh::entitymodule::C_Item *> created;
            for (SDK::wh::entitymodule::C_Item *item : inventory->GetItems()) {
                if (item != nullptr && !item->IsEquipped() && _stocked.find(item->GetId()) != _stocked.end()) {
                    created.push_back(item);
                }
            }
            if (!created.empty()) {
                inventory->DeleteItems(created, false);
            }
        }
        _stocked.clear();
    }

    void PickpocketModule::Abandon(bool tellServer) {
        if (tellServer) {
            SendResult(Shared::RPC::PickpocketOutcome::Abandoned, {});
        }

        // Idle before the native end runs, so nothing it reaches reports again.
        SDK::wh::playermodule::C_Pickpocketing *minigame = _minigame;
        _phase                                           = Phase::Idle;
        if (minigame != nullptr && IsRunning(minigame)) {
            minigame->Finish();
        }
        Unstock();
        Clear();
    }

    void PickpocketModule::SendResult(Shared::RPC::PickpocketOutcome outcome, const std::vector<Shared::RPC::PickpocketItem> &taken) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || _requestId == 0) {
            return;
        }
        Shared::RPC::PickpocketResult result;
        result.requestId = _requestId;
        result.outcome   = static_cast<std::uint8_t>(outcome);
        result.taken     = taken;
        net->BroadcastRPC(result, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PickpocketModule::Clear() {
        _phase           = Phase::Idle;
        _minigame        = nullptr;
        _victimEntityId  = 0;
        _victimNetworkId = 0;
        _requestId       = 0;
        _deadline        = 0;
        _buildingGrid    = false;
    }

    SDK::wh::entitymodule::C_Inventory *PickpocketModule::VictimInventory() const {
        // By network id, a map lookup, and only while it is still the same
        // body: a respawned player's new body is not the one that was stocked.
        KCDC::Features::Player::Human *victim        = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(_victimNetworkId);
        SDK::wh::entitymodule::C_Actor *actor        = victim != nullptr && victim->GetEntityId() == _victimEntityId ? victim->ResolveActor() : nullptr;
        SDK::wh::rpgmodule::C_InventorySoul *runtime = actor != nullptr ? actor->GetInventorySoulRuntime() : nullptr;
        return runtime != nullptr && runtime->IsInitialisedFor(actor->GetSoul()) ? runtime->GetInventory() : nullptr;
    }
} // namespace KCDC::Features::Pickpocket
