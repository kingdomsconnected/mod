#include "features/vendor/vendor_service.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"

#include "core/server.h"
#include "features/inventory/inventory.h"
#include "features/inventory/inventory_service.h"
#include "features/player/player_roster.h"
#include "features/vendor/vendor_scripting.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace KCDC::Features::Vendor {
    namespace {
        constexpr const char *kLogger = "Vendor";

        Shared::Entities::HumanEntity *ResolvePlayer(std::uint64_t playerId) {
            auto *replication = Framework::CoreModules::GetReplication();
            return replication != nullptr && playerId != 0 ? replication->GetEntity<Shared::Entities::HumanEntity>(playerId) : nullptr;
        }

        const Shared::Game::ItemClassEntry &Money() {
            static const Shared::Game::ItemClassEntry *money = Shared::Game::FindItemClass(Shared::Game::kMoneyItemClassGuid);
            return *money;
        }

        bool FitsOnWire(std::size_t rows) {
            return rows <= Shared::RPC::kMaxVendorRows;
        }
    } // namespace

    /** One basket, priced against the vendor's lists and ready to settle. */
    struct VendorService::Settlement {
        std::uint32_t vendorId  = 0;
        std::uint32_t sessionId = 0;
        std::uint64_t playerId  = 0;

        std::vector<TradeLine> bought;
        std::vector<TradeLine> sold;

        /** What the bought lines cost, and what the sold ones pay out. */
        std::uint64_t cost   = 0;
        std::uint64_t payout = 0;
    };

    VendorService &VendorService::Get() {
        static VendorService instance;
        return instance;
    }

    void VendorService::Install() {
        if (_installed) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _installed = true;

        net->RegisterRPC<Shared::RPC::VendorBasket>([](const Shared::RPC::VendorBasket &basket, MafiaNet::Packet *packet) {
            VendorService::Get().OnBasket(basket, packet);
        });
        net->RegisterRPC<Shared::RPC::VendorClose>([](const Shared::RPC::VendorClose &message, MafiaNet::Packet *packet) {
            VendorService::Get().OnClose(message, packet);
        });
    }

    std::uint32_t VendorService::Create(std::string name, std::optional<std::uint32_t> purse, bool buys) {
        const std::uint32_t vendorId = _nextVendor++;
        if (_nextVendor == 0) {
            _nextVendor = 1;
        }
        Vendor vendor;
        vendor.name            = std::move(name);
        vendor.purse           = purse;
        vendor.buysFromPlayers = buys;
        _vendors.insert_or_assign(vendorId, std::move(vendor));
        return vendorId;
    }

    bool VendorService::Destroy(std::uint32_t vendorId) {
        if (Find(vendorId) == nullptr) {
            return false;
        }

        // Collected first: closing erases from the map the walk stands in.
        std::vector<std::uint32_t> sessions;
        for (const auto &[sessionId, session] : _sessions) {
            if (session.vendorId == vendorId) {
                sessions.push_back(sessionId);
            }
        }
        for (const std::uint32_t sessionId : sessions) {
            Close(sessionId, Shared::RPC::VendorCloseReason::Server);
        }

        _vendors.erase(vendorId);
        return true;
    }

    bool VendorService::SetStock(std::uint32_t vendorId, std::vector<StockRow> stock) {
        for (auto &row : stock) {
            std::string error;
            nlohmann::json normalized;
            if (row.item == nullptr || !KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(std::string(row.item->guid), row.metadata, normalized, error)) {
                return false;
            }
            row.metadata = std::move(normalized);
        }
        Vendor *vendor = Find(vendorId);
        if (vendor == nullptr || !FitsOnWire(stock.size())) {
            return false;
        }
        vendor->stock = std::move(stock);
        Changed(vendorId, *vendor);
        return true;
    }

    bool VendorService::SetBuys(std::uint32_t vendorId, std::vector<BuyRow> buys) {
        Vendor *vendor = Find(vendorId);
        if (vendor == nullptr || !FitsOnWire(buys.size())) {
            return false;
        }
        vendor->buys = std::move(buys);
        Changed(vendorId, *vendor);
        return true;
    }

    bool VendorService::SetPurse(std::uint32_t vendorId, std::optional<std::uint32_t> purse) {
        Vendor *vendor = Find(vendorId);
        if (vendor == nullptr) {
            return false;
        }
        // Whether the purse is bottomless is fixed by the session's open, so a
        // vendor switching between the two is only reflected on the next one.
        vendor->purse = purse;
        Changed(vendorId, *vendor);
        return true;
    }

    std::optional<std::uint32_t> VendorService::GetPurse(std::uint32_t vendorId) const {
        const auto it = _vendors.find(vendorId);
        return it != _vendors.end() ? it->second.purse : std::nullopt;
    }

    std::uint32_t VendorService::Open(std::uint32_t vendorId, std::uint64_t playerId, std::uint64_t npcNetworkId) {
        Vendor *vendor = Find(vendorId);
        auto *player   = ResolvePlayer(playerId);
        auto *net      = Framework::CoreModules::GetNetworkPeer();
        if (vendor == nullptr || player == nullptr || net == nullptr) {
            return 0;
        }

        if (const std::uint32_t existing = SessionOf(playerId); existing != 0) {
            Close(existing, Shared::RPC::VendorCloseReason::Replaced);
        }

        const std::uint32_t sessionId = _nextSession++;
        if (_nextSession == 0) {
            _nextSession = 1;
        }
        Session session;
        session.vendorId     = vendorId;
        session.playerId     = playerId;
        session.npcNetworkId = npcNetworkId;

        Shared::RPC::VendorOpen open;
        open.sessionId    = sessionId;
        open.npcNetworkId = npcNetworkId;
        if (vendor->buysFromPlayers) {
            open.flags |= static_cast<std::uint8_t>(Shared::RPC::VendorFlags::Buys);
        }
        if (!vendor->purse.has_value()) {
            open.flags |= static_cast<std::uint8_t>(Shared::RPC::VendorFlags::BottomlessPurse);
        }
        net->SendRPC(open, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        SendStock(sessionId, session, *vendor);

        _sessions.insert_or_assign(sessionId, session);
        _byPlayer[playerId] = sessionId;
        Framework::Logging::GetLogger(kLogger)->info("Player {} opened vendor {} ('{}') as session {}", playerId, vendorId, vendor->name, sessionId);
        return sessionId;
    }

    bool VendorService::Close(std::uint32_t sessionId, Shared::RPC::VendorCloseReason reason) {
        Session *session = FindSession(sessionId);
        if (session == nullptr) {
            return false;
        }
        const Session closed = *session;

        auto *net    = Framework::CoreModules::GetNetworkPeer();
        auto *player = ResolvePlayer(closed.playerId);
        if (net != nullptr && player != nullptr) {
            Shared::RPC::VendorClose message;
            message.sessionId = sessionId;
            message.reason    = static_cast<std::uint8_t>(reason);
            net->SendRPC(message, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }

        _sessions.erase(sessionId);
        _byPlayer.erase(closed.playerId);
        Scripting::Events::VendorClosed(closed.vendorId, closed.playerId, static_cast<std::uint8_t>(reason));
        return true;
    }

    std::uint32_t VendorService::SessionOf(std::uint64_t playerId) const {
        const auto it = _byPlayer.find(playerId);
        return it != _byPlayer.end() ? it->second : 0;
    }

    std::uint32_t VendorService::VendorOf(std::uint32_t sessionId) const {
        const auto it = _sessions.find(sessionId);
        return it != _sessions.end() ? it->second.vendorId : 0;
    }

    void VendorService::Forget(std::uint64_t playerId) {
        const std::uint32_t sessionId = SessionOf(playerId);
        if (sessionId == 0) {
            return;
        }
        const std::uint32_t vendorId = VendorOf(sessionId);
        _sessions.erase(sessionId);
        _byPlayer.erase(playerId);
        // No message: the peer this would go to is the one that just left.
        Scripting::Events::VendorClosed(vendorId, playerId, static_cast<std::uint8_t>(Shared::RPC::VendorCloseReason::Interrupted));
    }

    void VendorService::OnClose(const Shared::RPC::VendorClose &message, MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (packet == nullptr || !message.IsSane() || replication == nullptr) {
            return;
        }
        auto *player     = replication->GetViewerAs<Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        Session *session = FindSession(message.sessionId);
        // A close for a session that is not this player's own, or that the
        // server already ended, is a late report about something gone.
        if (player == nullptr || session == nullptr || session->playerId != player->GetNetworkID()) {
            return;
        }
        const Session closed = *session;
        _sessions.erase(message.sessionId);
        _byPlayer.erase(closed.playerId);
        Scripting::Events::VendorClosed(closed.vendorId, closed.playerId, message.reason);
    }

    void VendorService::OnBasket(const Shared::RPC::VendorBasket &basket, MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (packet == nullptr || !basket.IsSane() || replication == nullptr) {
            return;
        }
        auto *player     = replication->GetViewerAs<Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        Session *session = FindSession(basket.sessionId);
        if (player == nullptr || session == nullptr || session->playerId != player->GetNetworkID()) {
            return;
        }
        Vendor *vendor = Find(session->vendorId);
        if (vendor == nullptr) {
            return;
        }

        if (basket.revision != vendor->revision) {
            // The screen is showing a list the server has replaced; hand it the
            // current one along with the refusal so the next try can land.
            SendResult(session->playerId, basket.sessionId, Shared::RPC::VendorOutcome::StaleList);
            SendStock(basket.sessionId, *session, *vendor);
            return;
        }

        Settlement settlement;
        settlement.vendorId  = session->vendorId;
        settlement.sessionId = basket.sessionId;
        settlement.playerId  = session->playerId;

        Shared::RPC::VendorOutcome outcome = Price(*vendor, basket, settlement);
        if (outcome == Shared::RPC::VendorOutcome::Accepted) {
            outcome = Settle(basket, settlement);
        }
        if (outcome != Shared::RPC::VendorOutcome::Accepted) {
            Framework::Logging::GetLogger(kLogger)->info("Player {} could not trade at vendor {} (outcome {})", settlement.playerId, settlement.vendorId, static_cast<int>(outcome));
            SendResult(settlement.playerId, settlement.sessionId, outcome);
            return;
        }
        Settled(settlement);
    }

    Shared::RPC::VendorOutcome VendorService::Price(const Vendor &vendor, const Shared::RPC::VendorBasket &basket, Settlement &settlement) const {
        // The screen may split one class across several lines -- one per stack
        // it moved -- so lines are merged before any of them is checked.
        struct Merged {
            Shared::RPC::ItemClassId itemClass;
            std::uint8_t side    = 0;
            std::uint64_t amount = 0;
        };
        std::vector<Merged> merged;
        for (const auto &line : basket.lines) {
            auto it = std::find_if(merged.begin(), merged.end(), [&line](const Merged &entry) {
                return entry.itemClass == line.itemClass && entry.side == line.side;
            });
            if (it == merged.end()) {
                merged.push_back(Merged {line.itemClass, line.side, line.amount});
            }
            else {
                it->amount += line.amount;
            }
        }

        for (const auto &line : merged) {
            if (line.amount > Shared::RPC::kMaxGiveAmount) {
                return Shared::RPC::VendorOutcome::TooLarge;
            }
            const auto amount = static_cast<std::uint32_t>(line.amount);

            if (line.side == static_cast<std::uint8_t>(Shared::RPC::VendorSide::Buy)) {
                const auto row = std::find_if(vendor.stock.begin(), vendor.stock.end(), [&line](const StockRow &stock) {
                    return Shared::Game::ItemClassIdFromGuid(stock.item->guid) == line.itemClass;
                });
                if (row == vendor.stock.end()) {
                    return Shared::RPC::VendorOutcome::NotTraded;
                }
                if (amount > row->amount) {
                    return Shared::RPC::VendorOutcome::OutOfStock;
                }
                settlement.bought.push_back(TradeLine {row->item, amount, row->price, row->metadata});
                settlement.cost += static_cast<std::uint64_t>(row->price) * amount;
            }
            else {
                if (!vendor.buysFromPlayers) {
                    return Shared::RPC::VendorOutcome::NotTraded;
                }
                const auto row = std::find_if(vendor.buys.begin(), vendor.buys.end(), [&line](const BuyRow &buy) {
                    return Shared::Game::ItemClassIdFromGuid(buy.item->guid) == line.itemClass;
                });
                if (row == vendor.buys.end()) {
                    return Shared::RPC::VendorOutcome::NotTraded;
                }
                settlement.sold.push_back(TradeLine {row->item, amount, row->price});
                settlement.payout += static_cast<std::uint64_t>(row->price) * amount;
            }
        }

        // Both sides are bounded, not just the balance: each is a count of
        // money units a script is told about, whatever the two net to.
        if (settlement.cost > Shared::RPC::kMaxVendorDealMoney || settlement.payout > Shared::RPC::kMaxVendorDealMoney) {
            return Shared::RPC::VendorOutcome::TooLarge;
        }
        if (vendor.purse.has_value() && settlement.payout > static_cast<std::uint64_t>(*vendor.purse) + settlement.cost) {
            return Shared::RPC::VendorOutcome::VendorCannotAfford;
        }
        return Shared::RPC::VendorOutcome::Accepted;
    }

    Shared::RPC::VendorOutcome VendorService::Settle(const Shared::RPC::VendorBasket &basket, Settlement &settlement) {
        auto &inventories = KCDC::Server::Get()->GetInventory();
        auto *inventory   = inventories.Find(settlement.playerId);
        const auto *worn  = inventories.Equipped(settlement.playerId);
        if (inventory == nullptr || worn == nullptr || !inventories.IsReady(settlement.playerId)) {
            return Shared::RPC::VendorOutcome::InventoryUnavailable;
        }
        const auto &rows = inventory->Rows();
        // Worn gear stays on the body, so it can be neither sold nor spent.
        const auto spendable = [&](const std::string &id) -> std::uint64_t {
            const auto row                = rows.find(id);
            const auto equipped           = worn->find(id);
            const std::uint64_t available = row != rows.end() ? row->second.amount : 0;
            const std::uint64_t held      = equipped != worn->end() ? equipped->second : 0;
            return available > held ? available - held : 0;
        };

        // The sold units must be exactly the sell lines: every unit a row this
        // player can hand over, and the units per class adding up to what was
        // priced -- nothing more slipped in, nothing short.
        std::map<std::string, std::uint64_t> debits;
        std::map<std::string, std::uint64_t> perClass;
        for (const auto &unit : basket.soldUnits) {
            const auto row = rows.find(unit.id);
            if (row == rows.end()) {
                return Shared::RPC::VendorOutcome::ItemsMissing;
            }
            debits[unit.id] += unit.amount;
            perClass[row->second.itemClass] += unit.amount;
        }
        for (const auto &[id, amount] : debits) {
            if (amount > spendable(id)) {
                return Shared::RPC::VendorOutcome::ItemsMissing;
            }
        }
        std::map<std::string, std::uint64_t> priced;
        for (const TradeLine &line : settlement.sold) {
            priced[std::string(line.item->guid)] += line.amount;
        }
        if (perClass != priced) {
            return Shared::RPC::VendorOutcome::ItemsMissing;
        }

        // Rows hold the item table's own spelling: every write resolves the
        // class exactly through FindItemClass.
        const std::string money(Money().guid);
        if (settlement.cost > settlement.payout) {
            std::uint64_t owed = settlement.cost - settlement.payout;
            for (const auto &[id, row] : rows) {
                if (owed == 0) {
                    break;
                }
                if (row.itemClass != money) {
                    continue;
                }
                // A row can be both offered and spent; what is offered is gone.
                const std::uint64_t offered = debits.contains(id) ? debits.at(id) : 0;
                const std::uint64_t left    = spendable(id) > offered ? spendable(id) - offered : 0;
                const auto amount           = std::min(owed, left);
                if (amount != 0) {
                    debits[id] += amount;
                    owed -= amount;
                }
            }
            if (owed != 0) {
                return Shared::RPC::VendorOutcome::PlayerCannotAfford;
            }
        }

        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        for (const auto &[id, amount] : debits) {
            units.push_back({id, static_cast<std::uint32_t>(amount)});
        }
        std::vector<KCDC::Features::Inventory::InventoryCredit> credits;
        for (const TradeLine &line : settlement.bought) {
            credits.push_back({std::string(line.item->guid), line.amount, line.metadata});
        }
        if (settlement.payout > settlement.cost) {
            credits.push_back({money, static_cast<std::uint32_t>(settlement.payout - settlement.cost)});
        }

        // One commit: what is sold and paid leaves, what is bought and any
        // change arrives, or none of it happens.
        std::vector<TradeLine> soldRows;
        for (const auto &unit : basket.soldUnits) {
            const auto &row = rows.at(unit.id);
            const auto line = std::find_if(settlement.sold.begin(), settlement.sold.end(), [&](const TradeLine &value) {
                return value.item->guid == row.itemClass;
            });
            soldRows.push_back({line->item, unit.amount, line->price, row.metadata});
        }
        const auto result = inventory->Exchange(units, credits, std::nullopt, "trade");
        if (!result.ok) {
            Framework::Logging::GetLogger(kLogger)->warn("Player {} could not settle a basket at vendor {}: {}", settlement.playerId, settlement.vendorId, result.code);
            if (result.code == "inventoryCapacity") {
                return Shared::RPC::VendorOutcome::InventoryFull;
            }
            return result.code == "invalidItem" || result.code == "invalidAmount" ? Shared::RPC::VendorOutcome::NotTraded : Shared::RPC::VendorOutcome::ItemsMissing;
        }
        inventories.DispatchChanges();
        settlement.sold = std::move(soldRows);
        return Shared::RPC::VendorOutcome::Accepted;
    }

    void VendorService::Settled(const Settlement &settlement) {
        if (Vendor *vendor = Find(settlement.vendorId)) {
            for (const TradeLine &line : settlement.bought) {
                const auto row = std::find_if(vendor->stock.begin(), vendor->stock.end(), [&line](const StockRow &stock) {
                    return stock.item == line.item;
                });
                if (row != vendor->stock.end()) {
                    row->amount -= std::min(row->amount, line.amount);
                }
            }
            std::erase_if(vendor->stock, [](const StockRow &stock) {
                return stock.amount == 0;
            });

            if (vendor->purse.has_value()) {
                // Price() proved the payout fits inside purse + cost, so this
                // never goes below zero; the clamp is for the far end.
                const std::int64_t balance = static_cast<std::int64_t>(*vendor->purse) + static_cast<std::int64_t>(settlement.cost) - static_cast<std::int64_t>(settlement.payout);
                vendor->purse              = static_cast<std::uint32_t>(std::clamp<std::int64_t>(balance, 0, std::numeric_limits<std::uint32_t>::max()));
            }
            Changed(settlement.vendorId, *vendor);
        }

        SendResult(settlement.playerId, settlement.sessionId, Shared::RPC::VendorOutcome::Accepted);

        const std::int64_t balance = static_cast<std::int64_t>(settlement.payout) - static_cast<std::int64_t>(settlement.cost);
        Framework::Logging::GetLogger(kLogger)->info("Player {} traded at vendor {}: {} line(s) bought, {} sold, balance {}", settlement.playerId, settlement.vendorId, settlement.bought.size(), settlement.sold.size(), balance);
        Scripting::Events::VendorTrade(settlement.vendorId, settlement.playerId, settlement.bought, settlement.sold, balance);
    }

    void VendorService::Changed(std::uint32_t vendorId, Vendor &vendor) {
        vendor.revision = vendor.revision == std::numeric_limits<std::uint32_t>::max() ? 1 : vendor.revision + 1;
        for (const auto &[sessionId, session] : _sessions) {
            if (session.vendorId == vendorId) {
                SendStock(sessionId, session, vendor);
            }
        }
    }

    void VendorService::SendStock(std::uint32_t sessionId, const Session &session, const Vendor &vendor) const {
        auto *net    = Framework::CoreModules::GetNetworkPeer();
        auto *player = ResolvePlayer(session.playerId);
        if (net == nullptr || player == nullptr) {
            return;
        }

        Shared::RPC::VendorStock message;
        message.sessionId = sessionId;
        message.revision  = vendor.revision;
        message.purse     = vendor.purse.value_or(0);
        for (const StockRow &row : vendor.stock) {
            Shared::RPC::VendorStockRow wire {Shared::Game::ItemClassIdFromGuid(row.item->guid), row.amount, row.price};
            wire.properties.inventoryItemId = 1;
            wire.properties.amount          = wire.amount;
            wire.properties.itemClass       = wire.itemClass;
            KCDC::Shared::Inventory::ApplyItemProperties(KCDC::Shared::Game::CustomItemRegistry::Get().PublicMetadata(row.metadata), wire.properties);
            message.stock.push_back(std::move(wire));
        }
        if (vendor.buysFromPlayers) {
            for (const BuyRow &row : vendor.buys) {
                message.buys.push_back(Shared::RPC::VendorBuyRow {Shared::Game::ItemClassIdFromGuid(row.item->guid), row.price});
            }
        }
        net->SendRPC(message, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void VendorService::SendResult(std::uint64_t playerId, std::uint32_t sessionId, Shared::RPC::VendorOutcome outcome) const {
        auto *net    = Framework::CoreModules::GetNetworkPeer();
        auto *player = ResolvePlayer(playerId);
        if (net == nullptr || player == nullptr) {
            return;
        }
        Shared::RPC::VendorResult result;
        result.sessionId = sessionId;
        result.outcome   = static_cast<std::uint8_t>(outcome);
        net->SendRPC(result, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    VendorService::Vendor *VendorService::Find(std::uint32_t vendorId) {
        const auto it = _vendors.find(vendorId);
        return it != _vendors.end() ? &it->second : nullptr;
    }

    VendorService::Session *VendorService::FindSession(std::uint32_t sessionId) {
        const auto it = _sessions.find(sessionId);
        return it != _sessions.end() ? &it->second : nullptr;
    }
} // namespace KCDC::Features::Vendor
