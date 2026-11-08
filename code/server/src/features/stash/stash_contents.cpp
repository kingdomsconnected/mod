#include "stash_contents.h"
#include "shared/game/custom_item_registry.h"

#include "shared/features/stash/stash_rpc.h"

#include <cmath>
#include <set>
#include <utility>

namespace KCDC::Features::Stash {
    bool StashContents::ReadTransfer(const std::string &body, StashTransfer &transfer) {
        const auto request = nlohmann::json::parse(body, nullptr, false);
        if (!request.is_object()) {
            return false;
        }
        for (const char *key : {"stash", "playerRevision", "stashRevision"}) {
            if (!request.contains(key) || !request.at(key).is_number_unsigned()) {
                return false;
            }
        }
        if (!request.contains("deposit") || !request.at("deposit").is_boolean() || !request.contains("units") || !request.at("units").is_array() || request.at("units").empty() || request.at("units").size() > KCDC::Shared::RPC::kMaxStashItems) {
            return false;
        }
        StashTransfer parsed {request.at("stash").get<std::uint64_t>(), request.at("deposit").get<bool>(), {}, request.at("playerRevision").get<std::uint64_t>(), request.at("stashRevision").get<std::uint64_t>()};
        std::set<std::string> seen;
        for (const auto &unit : request.at("units")) {
            if (!unit.is_object() || !unit.contains("id") || !unit.at("id").is_string() || !unit.contains("amount") || !unit.at("amount").is_number_unsigned()) {
                return false;
            }
            const auto amount = unit.at("amount").get<std::uint64_t>();
            if (amount == 0 || amount > KCDC::Features::Inventory::Inventory::kMaximumAmount || !seen.insert(unit.at("id").get<std::string>()).second) {
                return false;
            }
            parsed.units.push_back({unit.at("id").get<std::string>(), static_cast<std::uint32_t>(amount)});
        }
        transfer = std::move(parsed);
        return true;
    }

    bool StashContents::CanAccess(std::uint32_t stashWorld, std::uint32_t playerWorld, bool canAct, float distance, float maxDistance, bool isVirtual) {
        return stashWorld == playerWorld && canAct && (isVirtual || (std::isfinite(distance) && distance <= maxDistance));
    }

    nlohmann::json StashContents::Snapshot(std::uint64_t stashNetworkId, const KCDC::Features::Inventory::Inventory &inventory, const std::string &code) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto &[id, row] : inventory.Rows()) {
            auto encoded        = KCDC::Features::Inventory::Inventory::EncodeRow(row);
            encoded["metadata"] = KCDC::Shared::Game::CustomItemRegistry::Get().PublicMetadata(row.metadata);
            rows.push_back(std::move(encoded));
        }
        return {{"stash", stashNetworkId}, {"revision", inventory.Revision()}, {"code", code}, {"rows", std::move(rows)}};
    }

    KCDC::Features::Inventory::Inventory &StashContents::Create(std::uint64_t contentsId) {
        auto &stock = _stock[contentsId];
        if (!stock) {
            stock = std::make_unique<KCDC::Features::Inventory::Inventory>(_ids, KCDC::Shared::RPC::kMaxStashItems);
        }
        return *stock;
    }

    KCDC::Features::Inventory::Inventory *StashContents::Find(std::uint64_t contentsId) {
        const auto found = _stock.find(contentsId);
        return found == _stock.end() ? nullptr : found->second.get();
    }

    bool StashContents::HasContents(std::uint64_t contentsId) const {
        const auto found = _stock.find(contentsId);
        return found != _stock.end() && !found->second->Rows().empty();
    }

    bool StashContents::Open(const StashIdentity &stash, std::uint64_t playerId, std::uint64_t nowMs) {
        if (Find(stash.contentsId) == nullptr || playerId == 0) {
            return false;
        }
        if (const auto held = _views.find(stash.contentsId); held != _views.end() && held->second.playerId != playerId) {
            return false;
        }
        _views[stash.contentsId] = {stash, playerId, nowMs + kViewTimeoutMs};
        _events.push_back({"stashOpen", stash, playerId, "", std::nullopt});
        return true;
    }

    bool StashContents::Close(const StashIdentity &stash, std::uint64_t playerId, const std::string &reason) {
        const auto held = _views.find(stash.contentsId);
        if (held == _views.end() || held->second.playerId != playerId) {
            return false;
        }
        End(stash.contentsId, reason);
        return true;
    }

    bool StashContents::Renew(std::uint64_t contentsId, std::uint64_t playerId, std::uint64_t nowMs) {
        const auto held = _views.find(contentsId);
        if (held == _views.end() || held->second.playerId != playerId || playerId == 0) {
            return false;
        }
        held->second.expiresAt = nowMs + kViewTimeoutMs;
        return true;
    }

    std::uint64_t StashContents::HolderOf(std::uint64_t contentsId) const {
        const auto held = _views.find(contentsId);
        return held == _views.end() ? 0 : held->second.playerId;
    }

    const StashIdentity *StashContents::ViewOf(std::uint64_t contentsId) const {
        const auto held = _views.find(contentsId);
        return held == _views.end() ? nullptr : &held->second.stash;
    }

    KCDC::Features::Inventory::InventoryResult StashContents::Transfer(const StashIdentity &stash, std::uint64_t playerId, KCDC::Features::Inventory::Inventory &player, const StashTransfer &transfer) {
        auto *stock = Find(stash.contentsId);
        if (stock == nullptr) {
            return {false, "unknownStash"};
        }
        if (HolderOf(stash.contentsId) != playerId || playerId == 0) {
            return {false, "notHolder"};
        }
        const auto result = transfer.deposit ? KCDC::Features::Inventory::Inventory::Transfer(player, *stock, transfer.units, transfer.playerRevision, transfer.stashRevision, "deposit")
                                             : KCDC::Features::Inventory::Inventory::Transfer(*stock, player, transfer.units, transfer.stashRevision, transfer.playerRevision, "withdraw");
        Changed(stash, playerId);
        return result;
    }

    KCDC::Features::Inventory::InventoryResult StashContents::Deposit(const StashIdentity &stash, std::uint64_t playerId, KCDC::Features::Inventory::Inventory &player, const std::vector<KCDC::Features::Inventory::InventoryUnit> &units) {
        auto *stock = Find(stash.contentsId);
        if (stock == nullptr) {
            return {false, "unknownStash"};
        }
        const auto result = KCDC::Features::Inventory::Inventory::Transfer(player, *stock, units, std::nullopt, std::nullopt, "drop");
        Changed(stash, playerId);
        return result;
    }

    void StashContents::Changed(const StashIdentity &stash, std::uint64_t playerId) {
        auto *stock = Find(stash.contentsId);
        if (stock == nullptr) {
            return;
        }
        for (auto &change : stock->TakeChanges()) {
            _events.push_back({"stashInventoryChanged", stash, playerId, change.reason, std::move(change)});
        }
    }

    std::vector<std::pair<StashIdentity, std::uint64_t>> StashContents::Views() const {
        std::vector<std::pair<StashIdentity, std::uint64_t>> views;
        for (const auto &[contentsId, view] : _views) {
            views.emplace_back(view.stash, view.playerId);
        }
        return views;
    }

    std::vector<StashIdentity> StashContents::Expire(std::uint64_t nowMs) {
        std::vector<StashIdentity> closed;
        for (auto view = _views.begin(); view != _views.end();) {
            const auto next = std::next(view);
            if (view->second.expiresAt <= nowMs) {
                closed.push_back(view->second.stash);
                End(view->first, "timeout");
            }
            view = next;
        }
        return closed;
    }

    std::vector<StashIdentity> StashContents::Release(std::uint64_t playerId) {
        std::vector<StashIdentity> closed;
        for (auto view = _views.begin(); view != _views.end();) {
            const auto next = std::next(view);
            if (view->second.playerId == playerId) {
                closed.push_back(view->second.stash);
                End(view->first, "disconnected");
            }
            view = next;
        }
        return closed;
    }

    void StashContents::Destroy(const StashIdentity &stash) {
        if (const auto *view = ViewOf(stash.contentsId); view != nullptr && (view->networkId == stash.networkId || stash.networkId == stash.contentsId)) {
            End(stash.contentsId, "destroyed");
        }
        if (stash.networkId == stash.contentsId) {
            if (auto *stock = Find(stash.contentsId)) {
                Changed(stash, 0);
                std::vector<KCDC::Features::Inventory::InventoryUnit> units;
                for (const auto &[id, row] : stock->Rows()) {
                    units.push_back({id, row.amount});
                }
                if (!units.empty() && stock->Remove(units, std::nullopt, "destroy").ok) {
                    Changed(stash, 0);
                }
                _stock.erase(stash.contentsId);
            }
        }
        _events.push_back({"stashDestroy", stash, 0, "", std::nullopt});
    }

    std::vector<StashEvent> StashContents::TakeEvents() {
        return std::exchange(_events, {});
    }

    void StashContents::End(std::uint64_t contentsId, const std::string &reason) {
        const auto held = _views.find(contentsId);
        if (held == _views.end()) {
            return;
        }
        _events.push_back({"stashClose", held->second.stash, held->second.playerId, reason, std::nullopt});
        _views.erase(held);
    }
} // namespace KCDC::Features::Stash
