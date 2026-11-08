#include "npc_inventory.h"

#include "features/inventory/inventory_gameplay.h"
#include "shared/game/equipment_catalog.h"
#include "shared/game/item_classes.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace KCDC::Features::Npc {
    namespace {
        bool IsCarriedOutfitClass(const std::string &item) {
            const auto *definition = KCDC::Shared::Game::FindItemClass(item);
            return definition != nullptr && definition->category == "QuickSlotContainer";
        }
    } // namespace

    bool NpcInventory::Seed(const std::vector<KCDC::Features::Inventory::InventoryRow> &rows, const std::vector<std::uint32_t> &equipped) {
        if (_ready || rows.size() != equipped.size()) {
            return false;
        }
        std::size_t wornRows = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].id.empty() || equipped[i] > rows[i].amount || (equipped[i] != 0 && !KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(rows[i].itemClass) && !IsCarriedOutfitClass(rows[i].itemClass))) {
                return false;
            }
            wornRows += equipped[i] != 0 && !IsCarriedOutfitClass(rows[i].itemClass) ? 1 : 0;
        }
        if (wornRows > KCDC::Features::Inventory::InventoryGameplay::kMaximumEquippedRows) {
            return false;
        }
        // Set assigns server identities in input order.
        const auto result = _inventory.Set(rows, std::nullopt);
        if (!result.ok) {
            return false;
        }
        for (std::size_t i = 0; i < result.outputs.size(); ++i) {
            if (equipped[i] != 0 && !IsCarriedOutfitClass(rows[i].itemClass)) {
                _equipped.emplace(result.outputs[i].id, equipped[i]);
            }
        }
        _ready = true;
        ClampEquipment();
        return true;
    }

    void NpcInventory::Restored(std::map<std::string, std::uint32_t> equipped, std::optional<bool> harvested) {
        Release();
        _equipped = std::move(equipped);
        _ready    = true;
        if (harvested) {
            _harvested = *harvested;
        }
        ClampEquipment();
    }

    bool NpcInventory::Dress(const std::vector<std::string> &classes) {
        if (!_ready || classes.size() > KCDC::Features::Inventory::InventoryGameplay::kMaximumEquippedRows) {
            return false;
        }
        std::map<std::string, std::uint32_t> required;
        for (const auto &item : classes) {
            if (!KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(item) && !IsCarriedOutfitClass(item)) {
                return false;
            }
            ++required[item];
        }
        std::vector<KCDC::Features::Inventory::InventoryCredit> credits;
        for (const auto &[item, count] : required) {
            std::uint64_t held = 0;
            for (const auto &[id, row] : _inventory.Rows()) {
                if (row.itemClass == item) {
                    held += row.amount;
                }
            }
            if (held < count) {
                credits.push_back({item, static_cast<std::uint32_t>(count - held), nlohmann::json::object()});
            }
        }
        if (!credits.empty() && !_inventory.Exchange({}, credits, std::nullopt, "outfit").ok) {
            return false;
        }
        _equipped.clear();
        for (const auto &[id, row] : _inventory.Rows()) {
            const auto count = std::min(required[row.itemClass], row.amount);
            if (count != 0 && !IsCarriedOutfitClass(row.itemClass)) {
                _equipped.emplace(id, count);
                required[row.itemClass] -= count;
            }
        }
        ClampEquipment();
        return true;
    }

    void NpcInventory::ClampEquipment() {
        KCDC::Features::Inventory::InventoryGameplay::ClampEquipment(_inventory, _equipped);
        std::erase_if(_equipped, [](const auto &entry) {
            return entry.second == 0;
        });
        _equipped = KCDC::Features::Inventory::InventoryGameplay::CountEquipment(_inventory, _equipped);
        // Removing an underlayer also unequips what requires it. Those items
        // remain in stock: a padded coif's helmet is still available to loot.
        // Iterate to a fixed point so chained dependencies are handled too.
        std::size_t previous;
        do {
            previous            = _equipped.size();
            std::uint64_t slots = 0;
            for (const auto &[id, count] : _equipped) {
                if (const auto *equipment = KCDC::Shared::Game::FindEquipmentClass(_inventory.Rows().at(id).itemClass)) {
                    slots |= equipment->slots;
                }
            }
            std::erase_if(_equipped, [&](const auto &entry) {
                const auto *equipment = KCDC::Shared::Game::FindEquipmentClass(_inventory.Rows().at(entry.first).itemClass);
                return equipment != nullptr && !equipment->CanWear(slots);
            });
        } while (_equipped.size() != previous);
    }

    bool NpcInventory::Holds(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now) const {
        return player != 0 && token != 0 && _holder == player && _token == token && _epoch == epoch && now < _expires;
    }

    bool NpcInventory::Open(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now, bool harvest, bool harvestable) {
        if (!_ready || player == 0 || token == 0 || epoch == 0 || (_holder != 0 && now < _expires) || (harvest ? (!harvestable || _harvested) : (harvestable && !_harvested))) {
            return false;
        }
        _holder     = player;
        _token      = token;
        _epoch      = epoch;
        _expires    = now + KCDC::Shared::NpcInventory::kLeaseMilliseconds;
        _harvesting = harvest;
        return true;
    }

    bool NpcInventory::Renew(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now) {
        if (!Holds(player, token, epoch, now)) {
            return false;
        }
        _expires = now + KCDC::Shared::NpcInventory::kLeaseMilliseconds;
        return true;
    }

    bool NpcInventory::Complete(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now) {
        if (!Holds(player, token, epoch, now)) {
            return false;
        }
        if (_harvested && !_harvesting) {
            return true;
        }
        if (!_harvesting) {
            return false;
        }
        _harvested  = true;
        _harvesting = false;
        return true;
    }

    bool NpcInventory::Close(std::uint64_t player, std::uint64_t token) {
        if (player == 0 || token == 0 || _holder != player || _token != token) {
            return false;
        }
        Release();
        return true;
    }

    bool NpcInventory::CanAccess(bool initialized, bool canAct, bool alive, bool lootable, bool visible, float distance) {
        return initialized && canAct && !alive && lootable && visible && std::isfinite(distance) && distance >= 0.0F && distance <= KCDC::Shared::NpcInventory::kUseDistance;
    }

    KCDC::Features::Inventory::InventoryResult NpcInventory::Transfer(KCDC::Features::Inventory::Inventory &playerInventory, std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now, const std::vector<KCDC::Features::Inventory::InventoryUnit> &units,
        bool deposit, std::uint64_t npcRevision, std::uint64_t playerRevision) {
        if (!Holds(player, token, epoch, now) || _harvesting) {
            return {false, "noLootGrant", _inventory.Revision(), {}};
        }
        const auto result = deposit ? KCDC::Features::Inventory::Inventory::Transfer(playerInventory, _inventory, units, playerRevision, npcRevision, "npcDeposit", true)
                                    : KCDC::Features::Inventory::Inventory::Transfer(_inventory, playerInventory, units, npcRevision, playerRevision, "loot", true);
        if (result.ok) {
            if (!deposit) {
                for (const auto &unit : units) {
                    if (auto worn = _equipped.find(unit.id); worn != _equipped.end()) {
                        worn->second -= std::min(worn->second, unit.amount);
                    }
                }
            }
            ClampEquipment();
            Renew(player, token, epoch, now);
        }
        return result;
    }

    void NpcInventory::Release() {
        _holder = _token = _expires = 0;
        _epoch                      = 0;
        _harvesting                 = false;
    }

    void NpcInventory::Revived() {
        Release();
        _harvested = false;
    }

    nlohmann::json NpcInventory::Snapshot() const {
        // Compact rows exceed Inventory's accounting by at most six bytes
        // each (ten-digit amounts and array separators). Equipment and the
        // reply envelope fit in the remaining space, even at maximum ids.
        static_assert(KCDC::Features::Inventory::Inventory::kMaximumBytes + 6 * KCDC::Features::Inventory::Inventory::kMaximumRows + (KCDC::Features::Inventory::Inventory::kMaximumIdLength + 14) * KCDC::Features::Inventory::InventoryGameplay::kMaximumEquippedRows + 1024
                      < KCDC::Shared::RPC::NpcInventoryReply::kMaximumBytes);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto &[id, row] : _inventory.Rows()) {
            rows.push_back({{"id", id}, {"item", row.itemClass}, {"amount", row.amount}, {"metadata", row.metadata}});
        }
        return {{"ready", _ready}, {"revision", _inventory.Revision()}, {"rows", std::move(rows)}, {"equipped", _equipped}, {"harvested", _harvested}, {"harvesting", _harvesting}, {"holder", _holder}, {"token", _token}};
    }
} // namespace KCDC::Features::Npc
