#include "vendor_module.h"
#include "features/items/custom_item_module.h"

#include "core/application.h"
#include "features/npc/npc.h"
#include "features/player/player_inventory_service.h"
#include "features/ui/ui_hud_notifications.h"
#include "features/vendor/vendor_scripting.h"
#include "common/module_support.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/framework/i_ui_shop.h"
#include "sdk/wh/guimodule/c_ui_shop.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"
#include "sdk/wh/guimodule/e_ui_state.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>

namespace KCDC::Features::Vendor {
    namespace {
        constexpr const char *kLogger = "Vendor";

        /** How long the lists and the keeper's body get to turn up after an open. */
        constexpr MafiaNet::Time kBuildTimeoutMs = 5000;

        /** How long the screen gets to come up once it was asked for. */
        constexpr MafiaNet::Time kOpenTimeoutMs = 3000;

        /** Frames the screen must stay down before the player counts as gone; a debounce, not a proven gap. */
        constexpr std::uint8_t kClosedFrames = 2;

        /** What a bottomless purse is stocked with: as much as one deal may ever pay out. */
        constexpr std::uint32_t kBottomlessPurse = static_cast<std::uint32_t>(Shared::RPC::kMaxVendorDealMoney);

        Shared::RPC::ItemClassId ClassOf(const SDK::wh::entitymodule::C_Item *item) {
            const SDK::wh::entitymodule::S_ItemClassGUID itemClass = item->InvokeGetItemClassRaw();
            return KCDC::Features::Items::CustomItemModule::Get().ToLogical(itemClass);
        }

        const char *Explain(Shared::RPC::VendorOutcome outcome) {
            switch (outcome) {
            case Shared::RPC::VendorOutcome::StaleList: return "The vendor's wares have changed. Check the prices and try again.";
            case Shared::RPC::VendorOutcome::NotTraded: return "The vendor does not trade in one of those.";
            case Shared::RPC::VendorOutcome::OutOfStock: return "The vendor does not have that many.";
            case Shared::RPC::VendorOutcome::VendorCannotAfford: return "The vendor cannot afford that.";
            case Shared::RPC::VendorOutcome::PlayerCannotAfford: return "You cannot afford that.";
            case Shared::RPC::VendorOutcome::ItemsMissing: return "Some of what you offered is no longer in your inventory.";
            case Shared::RPC::VendorOutcome::Busy: return "The vendor is busy with another customer. Try again.";
            case Shared::RPC::VendorOutcome::TooLarge: return "That is too much money for one deal.";
            case Shared::RPC::VendorOutcome::InventoryUnavailable: return "Your inventory is not ready yet. Try again in a moment.";
            case Shared::RPC::VendorOutcome::InventoryFull: return "You cannot carry all of that.";
            case Shared::RPC::VendorOutcome::Accepted: break;
            }
            return nullptr;
        }
    } // namespace

    VendorModule &VendorModule::Get() {
        static VendorModule instance;
        return instance;
    }

    std::optional<std::int32_t> VendorModule::PriceFor(const SDK::wh::shopmodule::C_Shop *shop, SDK::wh::entitymodule::C_Item *item, bool selling) const {
        if (_phase == Phase::Idle || shop == nullptr || item == nullptr || shop != _shop.GetShop()) {
            return std::nullopt;
        }
        const Shared::RPC::ItemClassId itemClass = ClassOf(item);
        if (selling) {
            // What the vendor does not buy is worth nothing to it. The sale
            // filter already greys it out; zero is for anything that slips past,
            // and the server refuses the line if it is offered anyway.
            const Shared::RPC::VendorBuyRow *row = FindBuy(itemClass);
            return static_cast<std::int32_t>(row != nullptr ? row->price : 0);
        }
        const Shared::RPC::VendorStockRow *row = FindStock(itemClass);
        if (row == nullptr) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(row->price);
    }

    bool VendorModule::InterceptCommit(SDK::wh::shopmodule::C_ShoppingTransaction *transaction) {
        // Keyed on the deal itself rather than on the shop it points at: a deal
        // left unbound after a trade still has to be swallowed here, because
        // the native commit would dereference the missing parties.
        if (_phase == Phase::Idle || transaction == nullptr || transaction != _shop.GetTransaction()) {
            return false;
        }
        if (transaction->GetShop() != _shop.GetShop()) {
            Framework::Logging::GetLogger(kLogger)->warn("A commit arrived on an unbound deal for session {}; dropped", _sessionId);
            return true;
        }
        SDK::wh::entitymodule::C_Inventory *stock = _shop.GetShop()->GetStock();
        const auto &inventory                     = KCDC::Core::gApplication->GetPlayerInventoryService();

        // Server rows the sell lines hand over. Named by kind, not by the
        // basket item's WUID: the screen moves part of a stack as a split
        // with a WUID of its own and folds what it moves into one basket
        // item (C_ItemIndexer::AddItemAmount), so a basket item is rarely a
        // stack the projection made. Rows of one kind are interchangeable.
        std::map<std::string, std::uint32_t> offered;

        Shared::RPC::VendorBasket basket;
        basket.sessionId = _sessionId;
        basket.revision  = _revision;
        for (std::size_t index = 0; index < transaction->GetBasketCount(); ++index) {
            SDK::wh::entitymodule::C_Item *item = transaction->GetBasketItemAt(index);
            if (item == nullptr || item->GetAmount() == 0) {
                continue;
            }
            // The one test the native commit makes to split buys from sells.
            const auto side                          = item->GetOwner() == stock ? Shared::RPC::VendorSide::Buy : Shared::RPC::VendorSide::Sell;
            const Shared::RPC::ItemClassId itemClass = ClassOf(item);
            auto line                                = std::find_if(basket.lines.begin(), basket.lines.end(), [&](const Shared::RPC::VendorBasketLine &existing) {
                return existing.itemClass == itemClass && existing.side == static_cast<std::uint8_t>(side);
            });
            if (line == basket.lines.end()) {
                if (basket.lines.size() >= Shared::RPC::kMaxVendorBasketLines) {
                    Framework::Logging::GetLogger(kLogger)->warn("Basket holds more than {} kinds of item; the rest are left out", Shared::RPC::kMaxVendorBasketLines);
                    break;
                }
                basket.lines.push_back(Shared::RPC::VendorBasketLine {itemClass, 0, static_cast<std::uint8_t>(side)});
                line = std::prev(basket.lines.end());
            }
            line->amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(line->amount) + item->GetAmount(), Shared::RPC::kMaxGiveAmount));
            if (side == Shared::RPC::VendorSide::Sell) {
                // A stack the projection cannot account for leaves the basket
                // short, and the server refuses it rather than choosing for
                // the player.
                std::uint32_t amount = item->GetAmount();
                for (const auto &unit : inventory.UnwornUnitsOfKind(*item)) {
                    std::uint32_t &taken = offered[unit.id];
                    const auto take      = std::min(amount, unit.amount - taken);
                    taken += take;
                    amount -= take;
                    if (amount == 0) {
                        break;
                    }
                }
                if (amount != 0) {
                    Framework::Logging::GetLogger(kLogger)->warn("Could not name the server rows of a stack offered for sale in session {}", _sessionId);
                }
            }
        }
        for (const auto &[id, amount] : offered) {
            if (amount != 0 && basket.soldUnits.size() < Shared::RPC::kMaxVendorSoldUnits) {
                basket.soldUnits.push_back(Shared::RPC::VendorSoldUnit {id, amount});
            }
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr && basket.IsSane()) {
            net->BroadcastRPC(basket, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            Framework::Logging::GetLogger(kLogger)->debug("Sent a basket of {} line(s) for session {}", basket.lines.size(), _sessionId);
        }
        // Ours either way: an empty basket has nothing to commit, and the
        // screen's own commit must never move anything at this shop.
        return true;
    }

    bool VendorModule::RefusesSale(const SDK::wh::entitymodule::C_Item *item) const {
        if (_phase == Phase::Idle || item == nullptr) {
            return false;
        }
        return !Buys() || FindBuy(ClassOf(item)) == nullptr;
    }

    void VendorModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }

        if (_nativeContextLost.exchange(false) && _phase != Phase::Idle) {
            _shop.Forget();
            Teardown(CloseCause::LevelChange);
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        switch (_phase) {
        case Phase::Idle: break;
        case Phase::AwaitingStock:
            if (_haveStock) {
                Build();
            }
            else if (now > _deadline) {
                Framework::Logging::GetLogger(kLogger)->warn("Session {} never received its lists", _sessionId);
                Teardown(CloseCause::Failed);
            }
            break;
        case Phase::Opening:
            if (IsScreenUp()) {
                _phase        = Phase::Open;
                _closedFrames = 0;
                _announced    = true;
                Scripting::Events::VendorOpened(_sessionId, _npcNetworkId);
            }
            else if (now > _deadline) {
                Framework::Logging::GetLogger(kLogger)->warn("The trade screen did not come up for session {}", _sessionId);
                Teardown(CloseCause::Failed);
            }
            break;
        case Phase::Open:
            _shop.Rebind();
            if (_restockPending && _shop.Restock(_stock, ShelfPurse())) {
                _restockPending = false;
            }
            if (IsScreenUp()) {
                _closedFrames = 0;
            }
            else if (++_closedFrames >= kClosedFrames) {
                Teardown(CloseCause::Player);
            }
            break;
        }
    }

    void VendorModule::Build() {
        if (!KCDC::Features::Items::CustomItemModule::Get().Ready()) {
            if (!KCDC::Features::Items::CustomItemModule::Get().Failure().empty() || MafiaNet::GetTime() > _deadline) {
                Teardown(CloseCause::Failed);
            }
            return;
        }
        for (const auto &row : _stock) {
            if (KCDC::Shared::Game::FindItemClass(KCDC::Shared::Game::CryGuidToString(row.itemClass.low, row.itemClass.high)) == nullptr) {
                if (MafiaNet::GetTime() > _deadline) {
                    Teardown(CloseCause::Failed);
                }
                return;
            }
        }
        // The keeper is the named NPC's soul, or the player's own when the
        // vendor is nobody in particular. An NPC whose body has not streamed in
        // yet is waited on, up to the same deadline as the lists.
        SDK::wh::rpgmodule::C_Soul *keeper = nullptr;
        if (_npcNetworkId != 0) {
            auto *replication = Framework::CoreModules::GetReplication();
            auto *npc         = replication != nullptr ? replication->GetEntity<Npc::Npc>(_npcNetworkId) : nullptr;
            auto *actor       = npc != nullptr ? npc->ResolveActor() : nullptr;
            keeper            = actor != nullptr ? actor->GetSoul() : nullptr;
        }
        else if (auto *human = KCDC::Common::LocalHumanActor()) {
            keeper = human->GetSoul();
        }
        SDK::IEntity *player = KCDC::Common::LocalPlayer();
        if (keeper == nullptr || player == nullptr) {
            if (MafiaNet::GetTime() > _deadline) {
                Framework::Logging::GetLogger(kLogger)->warn("No soul to keep the shop for session {} (NPC {})", _sessionId, _npcNetworkId);
                Teardown(CloseCause::Failed);
            }
            return;
        }

        if (!_shop.Build(SDK::wh::framework::WUID {keeper->GetWuid()}, player->GetWorldPos())) {
            Teardown(CloseCause::Failed);
            return;
        }
        if (!_shop.Restock(_stock, ShelfPurse())) {
            Teardown(CloseCause::Failed);
            return;
        }
        _restockPending = false;
        _shop.Open();

        _phase    = Phase::Opening;
        _deadline = MafiaNet::GetTime() + kOpenTimeoutMs;
        Framework::Logging::GetLogger(kLogger)->info("Opened session {}: {} to buy, {} priced to sell", _sessionId, _stock.size(), _buys.size());
    }

    void VendorModule::Teardown(CloseCause cause) {
        if (IsScreenUp()) {
            auto *environment                    = SDK::wh::C_GlobalEnvironment::GetInstance();
            SDK::wh::framework::I_UIShop *screen = environment != nullptr ? environment->GetUIShop() : nullptr;
            if (screen != nullptr) {
                screen->GetUIShop()->Close();
            }
        }
        _shop.Destroy();

        // The server already knows about the closes it caused, and a session
        // that ended with the connection has nobody left to tell.
        const bool report = cause == CloseCause::Player || cause == CloseCause::Failed || cause == CloseCause::LevelChange;
        if (report && _sessionId != 0) {
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                Shared::RPC::VendorClose message;
                message.sessionId = _sessionId;
                message.reason    = static_cast<std::uint8_t>(cause == CloseCause::Player ? Shared::RPC::VendorCloseReason::Closed : Shared::RPC::VendorCloseReason::Unavailable);
                net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }

        if (const char *reason = ScriptReason(cause); _announced && reason != nullptr) {
            Scripting::Events::VendorClosed(_sessionId, reason);
        }

        _phase          = Phase::Idle;
        _sessionId      = 0;
        _npcNetworkId   = 0;
        _flags          = 0;
        _revision       = 0;
        _purse          = 0;
        _haveStock      = false;
        _restockPending = false;
        _deadline       = 0;
        _closedFrames   = 0;
        _announced      = false;
        _stock.clear();
        _buys.clear();
    }

    void VendorModule::Reset() {
        // The session is gone with the connection; nobody is left to tell.
        Teardown(CloseCause::SessionOver);
        _rpcsInstalled     = false;
        _nativeContextLost = false;
    }

    const char *VendorModule::ScriptReason(CloseCause cause) {
        switch (cause) {
        case CloseCause::Player: return "player";
        case CloseCause::Server: return "server";
        case CloseCause::Replaced: return "replaced";
        case CloseCause::LevelChange: return "levelChange";
        case CloseCause::SessionOver: return "sessionOver";
        case CloseCause::Failed: break;
        }
        return nullptr;
    }

    void VendorModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    bool VendorModule::IsScreenUp() const {
        SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        return blockers != nullptr && blockers->IsHeld(SDK::wh::guimodule::E_UIState::Shop);
    }

    bool VendorModule::Buys() const {
        return (_flags & static_cast<std::uint8_t>(Shared::RPC::VendorFlags::Buys)) != 0;
    }

    std::uint32_t VendorModule::ShelfPurse() const {
        const bool bottomless = (_flags & static_cast<std::uint8_t>(Shared::RPC::VendorFlags::BottomlessPurse)) != 0;
        return bottomless ? kBottomlessPurse : _purse;
    }

    const Shared::RPC::VendorStockRow *VendorModule::FindStock(const Shared::RPC::ItemClassId &itemClass) const {
        const auto it = std::find_if(_stock.begin(), _stock.end(), [&itemClass](const Shared::RPC::VendorStockRow &row) {
            return row.itemClass == itemClass;
        });
        return it != _stock.end() ? &*it : nullptr;
    }

    const Shared::RPC::VendorBuyRow *VendorModule::FindBuy(const Shared::RPC::ItemClassId &itemClass) const {
        const auto it = std::find_if(_buys.begin(), _buys.end(), [&itemClass](const Shared::RPC::VendorBuyRow &row) {
            return row.itemClass == itemClass;
        });
        return it != _buys.end() ? &*it : nullptr;
    }

    void VendorModule::OnOpen(const Shared::RPC::VendorOpen &message) {
        // A second open replaces the first. The server has already closed that
        // one on its side, so there is nothing to report.
        if (_sessionId != 0) {
            Teardown(CloseCause::Replaced);
        }
        _sessionId    = message.sessionId;
        _npcNetworkId = message.npcNetworkId;
        _flags        = message.flags;
        _phase        = Phase::AwaitingStock;
        _deadline     = MafiaNet::GetTime() + kBuildTimeoutMs;
    }

    void VendorModule::OnStock(const Shared::RPC::VendorStock &message) {
        if (message.sessionId != _sessionId || _phase == Phase::Idle) {
            return;
        }
        _revision  = message.revision;
        _purse     = message.purse;
        _stock     = message.stock;
        _buys      = message.buys;
        _haveStock = true;
        if (_phase == Phase::Opening || _phase == Phase::Open) {
            _restockPending = true;
        }
    }

    void VendorModule::OnResult(const Shared::RPC::VendorResult &message) {
        if (message.sessionId != _sessionId) {
            return;
        }
        if (const char *text = Explain(static_cast<Shared::RPC::VendorOutcome>(message.outcome))) {
            UI::Hud::ShowNotification(text);
        }
    }

    void VendorModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _rpcsInstalled = true;

        net->RegisterRPC<Shared::RPC::VendorOpen>([](const Shared::RPC::VendorOpen &message, MafiaNet::Packet *) {
            if (message.IsSane()) {
                VendorModule::Get().OnOpen(message);
            }
        });
        net->RegisterRPC<Shared::RPC::VendorStock>([](const Shared::RPC::VendorStock &message, MafiaNet::Packet *) {
            if (message.IsSane()) {
                VendorModule::Get().OnStock(message);
            }
        });
        net->RegisterRPC<Shared::RPC::VendorResult>([](const Shared::RPC::VendorResult &message, MafiaNet::Packet *) {
            if (message.IsSane()) {
                VendorModule::Get().OnResult(message);
            }
        });
        net->RegisterRPC<Shared::RPC::VendorClose>([](const Shared::RPC::VendorClose &message, MafiaNet::Packet *) {
            VendorModule &self = VendorModule::Get();
            // A close for a session that is not the one on screen is a late
            // message about a session already gone; it must not take down
            // whatever replaced it.
            if (message.IsSane() && message.sessionId == self._sessionId) {
                self.Teardown(message.reason == static_cast<std::uint8_t>(Shared::RPC::VendorCloseReason::Replaced) ? CloseCause::Replaced : CloseCause::Server);
            }
        });
    }
} // namespace KCDC::Features::Vendor
