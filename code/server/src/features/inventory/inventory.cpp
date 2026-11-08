#include "inventory.h"

#include "shared/game/custom_item_registry.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>

namespace KCDC::Features::Inventory {
    namespace {
        std::string KindKey(const std::string &itemClass, const nlohmann::json &metadata) {
            return itemClass + '\n' + metadata.dump();
        }

        bool Normalize(const std::string &itemClass, const nlohmann::json &metadata, nlohmann::json &output, std::string &error) {
            if (KCDC::Shared::Game::FindItemClass(itemClass) == nullptr) {
                error = "invalidItem";
                return false;
            }
            return KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(itemClass, metadata, output, error);
        }

        KCDC::Shared::RPC::InventoryItem Parse(const std::string &itemClass, const nlohmann::json &metadata) {
            KCDC::Shared::RPC::InventoryItem item;
            item.inventoryItemId = 1;
            item.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid(itemClass);
            item.amount          = 1;
            KCDC::Shared::Inventory::ApplyItemProperties(metadata, item);
            return item;
        }
    } // namespace

    std::vector<InventoryChange> Inventory::TakeChanges() {
        std::vector<InventoryChange> changes;
        changes.swap(_changes);
        return changes;
    }

    std::map<std::string, std::int64_t> Inventory::TakeNotices() {
        std::map<std::string, std::int64_t> notices;
        notices.swap(_notices);
        return notices;
    }

    InventoryResult Inventory::Refuse(const std::string &code) {
        return {false, code, 0, {}};
    }

    std::size_t Inventory::RowBytes(const InventoryRow &row) {
        // What the row costs on the sync wire, with room for its JSON keys.
        // Private custom data is removed, but even a tiny custom object
        // gains a 64-character stacking token in its public form.
        return row.id.size() + row.itemClass.size() + row.metadata.dump().size() + 48 + (row.metadata.contains("custom") ? 96 : 0);
    }

    bool Inventory::ValidId(const std::string &id) {
        return !id.empty() && id.size() <= kMaximumIdLength && std::all_of(id.begin(), id.end(), [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == ':' || ch == '.';
        });
    }

    bool Inventory::IsStale(std::optional<std::uint64_t> revision) const {
        return revision.has_value() && *revision != _revision;
    }

    const InventoryRow *Inventory::Current(const std::string &id, const Edits &edits) const {
        if (const auto edited = edits.rows.find(id); edited != edits.rows.end()) {
            return edited->second ? &*edited->second : nullptr;
        }
        const auto found = _rows.find(id);
        return found == _rows.end() ? nullptr : &found->second;
    }

    std::string Inventory::NewId(const Edits &edits) const {
        std::string id;
        do
            id = _ids.Next();
        while (_rows.contains(id) || edits.rows.contains(id));
        return id;
    }

    std::optional<std::string> Inventory::CheckUnits(const std::vector<InventoryUnit> &units, const Edits &edits) const {
        if (units.empty() || units.size() > kMaximumRows) {
            return "invalidItems";
        }
        std::set<std::string> named;
        for (const auto &unit : units) {
            if (!named.insert(unit.id).second) {
                return "invalidItems";
            }
            if (unit.amount == 0) {
                return "invalidAmount";
            }
            const auto *row = Current(unit.id, edits);
            if (row == nullptr) {
                return "unknownItem";
            }
            if (unit.amount > row->amount) {
                return "insufficientItems";
            }
        }
        return std::nullopt;
    }

    void Inventory::Debit(const std::vector<InventoryUnit> &units, Edits &edits) const {
        for (const auto &unit : units) {
            InventoryRow row = *Current(unit.id, edits);
            row.amount -= unit.amount;
            edits.rows.insert_or_assign(unit.id, row.amount == 0 ? std::nullopt : std::optional<InventoryRow>(row));
        }
    }

    std::string Inventory::Credit(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, Edits &edits) const {
        if (const auto *custom = KCDC::Shared::Game::CustomItemRegistry::Get().Find(itemClass); custom != nullptr && !custom->stackable) {
            const auto id = NewId(edits);
            edits.rows.emplace(id, InventoryRow {id, itemClass, amount, metadata});
            return id;
        }
        const auto key  = KindKey(itemClass, metadata);
        const auto room = [amount](const InventoryRow &row) {
            return row.amount <= kMaximumAmount - amount;
        };
        std::string target;
        for (const auto &[id, row] : edits.rows) {
            if (row && room(*row) && KindKey(row->itemClass, row->metadata) == key) {
                target = id;
                break;
            }
        }
        if (const auto kind = _kinds.find(key); target.empty() && kind != _kinds.end()) {
            for (const auto &id : kind->second) {
                if (!edits.rows.contains(id) && room(_rows.at(id))) {
                    target = id;
                    break;
                }
            }
        }
        if (target.empty()) {
            target = NewId(edits);
            edits.rows.insert_or_assign(target, InventoryRow {target, itemClass, amount, metadata});
            return target;
        }
        InventoryRow row = *Current(target, edits);
        row.amount += amount;
        edits.rows.insert_or_assign(target, row);
        return target;
    }

    void Inventory::CreditUnits(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, Edits &edits, std::vector<InventoryUnit> &outputs) const {
        const auto *custom  = KCDC::Shared::Game::CustomItemRegistry::Get().Find(itemClass);
        const bool separate = custom != nullptr && !custom->stackable;
        // One row beyond capacity guarantees an atomic refusal without
        // allocating every requested unit of an oversized grant.
        const auto count = separate ? std::min<std::uint32_t>(amount, static_cast<std::uint32_t>(_maximumRows + 1)) : 1U;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto units = separate ? 1U : amount;
            outputs.push_back({Credit(itemClass, units, metadata, edits), units});
        }
    }

    std::optional<std::string> Inventory::CheckCapacity(const Edits &edits) const {
        std::size_t rows  = _rows.size();
        std::size_t bytes = _bytes;
        for (const auto &[id, after] : edits.rows) {
            if (const auto found = _rows.find(id); found != _rows.end()) {
                --rows;
                bytes -= RowBytes(found->second);
            }
            if (after) {
                ++rows;
                bytes += RowBytes(*after);
            }
        }
        if (rows > _maximumRows || bytes > kMaximumBytes) {
            return "inventoryCapacity";
        }
        return std::nullopt;
    }

    InventoryResult Inventory::Commit(Edits edits, const std::string &reason, std::vector<InventoryUnit> outputs, bool notify) {
        InventoryChange change;
        change.reason = reason;
        for (auto &[id, after] : edits.rows) {
            const auto found = _rows.find(id);
            std::optional<InventoryRow> before;
            if (found != _rows.end()) {
                before = found->second;
            }
            if (before == after) {
                continue;
            }
            if (before) {
                _bytes -= RowBytes(*before);
                auto kind = _kinds.find(KindKey(before->itemClass, before->metadata));
                kind->second.erase(id);
                if (kind->second.empty()) {
                    _kinds.erase(kind);
                }
                _rows.erase(found);
            }
            if (after) {
                _bytes += RowBytes(*after);
                _kinds[KindKey(after->itemClass, after->metadata)].insert(id);
                _rows.insert_or_assign(id, *after);
            }
            change.rows.push_back({id, std::move(before), std::move(after)});
        }
        if (!change.rows.empty()) {
            change.revision = ++_revision;
            // Counted per class, so units moving between rows are no news.
            if (notify) {
                for (const auto &row : change.rows) {
                    if (row.before) {
                        _notices[row.before->itemClass] -= row.before->amount;
                    }
                    if (row.after) {
                        _notices[row.after->itemClass] += row.after->amount;
                    }
                }
                std::erase_if(_notices, [](const auto &entry) {
                    return entry.second == 0;
                });
            }
            _changes.push_back(std::move(change));
        }
        return {true, "", _revision, std::move(outputs)};
    }

    InventoryResult Inventory::Add(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, std::optional<std::uint64_t> revision, const std::string &reason, bool notify) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        if (KCDC::Shared::Game::FindItemClass(itemClass) == nullptr) {
            return Refuse("invalidItem");
        }
        if (amount == 0 || amount > kMaximumAmount) {
            return Refuse("invalidAmount");
        }
        nlohmann::json canonical;
        std::string error;
        if (!Normalize(itemClass, metadata, canonical, error)) {
            return Refuse(error);
        }
        Edits edits;
        std::vector<InventoryUnit> outputs;
        CreditUnits(itemClass, amount, canonical, edits, outputs);
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), reason, std::move(outputs), notify);
    }

    InventoryResult Inventory::Remove(const std::vector<InventoryUnit> &units, std::optional<std::uint64_t> revision, const std::string &reason, bool notify) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        Edits edits;
        if (const auto invalid = CheckUnits(units, edits)) {
            return Refuse(*invalid);
        }
        Debit(units, edits);
        return Commit(std::move(edits), reason, {}, notify);
    }

    InventoryResult Inventory::Exchange(const std::vector<InventoryUnit> &debits, const std::vector<InventoryCredit> &credits, std::optional<std::uint64_t> revision, const std::string &reason, bool notify) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        if (debits.empty() && credits.empty()) {
            return Refuse("invalidItems");
        }
        Edits edits;
        if (!debits.empty()) {
            if (const auto invalid = CheckUnits(debits, edits)) {
                return Refuse(*invalid);
            }
            Debit(debits, edits);
        }
        std::vector<InventoryUnit> outputs;
        for (const auto &credit : credits) {
            if (credit.amount == 0 || credit.amount > kMaximumAmount) {
                return Refuse("invalidAmount");
            }
            nlohmann::json canonical;
            std::string error;
            if (!Normalize(credit.itemClass, credit.metadata, canonical, error)) {
                return Refuse(error);
            }
            CreditUnits(credit.itemClass, credit.amount, canonical, edits, outputs);
        }
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), reason, std::move(outputs), notify);
    }

    InventoryResult Inventory::Take(const std::string &itemClass, std::uint32_t amount, std::optional<std::uint64_t> revision, const std::string &reason, bool notify) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        if (amount == 0) {
            return Refuse("invalidAmount");
        }
        std::vector<InventoryUnit> units;
        std::uint32_t remaining = amount;
        for (const auto &[id, row] : _rows) {
            if (row.itemClass != itemClass) {
                continue;
            }
            const auto count = std::min(remaining, row.amount);
            units.push_back({id, count});
            remaining -= count;
            if (remaining == 0) {
                break;
            }
        }
        if (remaining != 0) {
            return Refuse("insufficientItems");
        }
        return Remove(units, std::nullopt, reason, notify);
    }

    const InventoryRow *Inventory::ValidateCustomUse(const std::string &id, std::uint64_t revision) const {
        const auto row = _rows.find(id);
        if (revision != _revision || row == _rows.end()) {
            return nullptr;
        }
        const auto *definition = KCDC::Shared::Game::CustomItemRegistry::Get().Find(row->second.itemClass);
        return definition != nullptr && definition->usable ? &row->second : nullptr;
    }

    InventoryResult Inventory::UpdateCustom(const std::string &id, const nlohmann::json &patch, std::optional<std::uint64_t> revision) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        const auto found = _rows.find(id);
        if (found == _rows.end()) {
            return Refuse("unknownItem");
        }
        if (KCDC::Shared::Game::CustomItemRegistry::Get().Find(found->second.itemClass) == nullptr) {
            return Refuse("invalidItem");
        }
        nlohmann::json normalized;
        if (!KCDC::Shared::Game::CustomItemRegistry::NormalizeCustom(patch, normalized)) {
            return Refuse("invalidMetadata");
        }
        auto metadata = found->second.metadata;
        auto custom   = metadata.value("custom", nlohmann::json::object());
        for (const auto &[key, value] : patch.items()) {
            if (value.is_null()) {
                custom.erase(key);
            }
            else {
                custom[key] = value;
            }
        }
        if (custom.empty()) {
            metadata.erase("custom");
        }
        else {
            metadata["custom"] = std::move(custom);
        }
        return SetProperties(id, metadata, revision);
    }

    InventoryResult Inventory::SetProperties(const std::string &id, const nlohmann::json &metadata, std::optional<std::uint64_t> revision) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        const auto found = _rows.find(id);
        if (found == _rows.end()) {
            return Refuse("unknownItem");
        }
        InventoryRow row = found->second;
        std::string error;
        if (!Normalize(row.itemClass, metadata, row.metadata, error)) {
            return Refuse(error);
        }
        Edits edits;
        edits.rows.insert_or_assign(id, row);
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), "properties", {{id, row.amount}});
    }

    InventoryResult Inventory::Set(const std::vector<InventoryRow> &rows, std::optional<std::uint64_t> revision) {
        if (IsStale(revision)) {
            return Refuse("staleRevision");
        }
        if (rows.size() > _maximumRows) {
            return Refuse("inventoryCapacity");
        }
        std::set<std::string> given;
        for (const auto &row : rows) {
            if (!row.id.empty() && (!ValidId(row.id) || !given.insert(row.id).second)) {
                return Refuse("invalidItems");
            }
        }
        Edits edits;
        for (const auto &[id, row] : _rows) {
            edits.rows.insert_or_assign(id, std::nullopt);
        }
        std::vector<InventoryUnit> outputs;
        for (const auto &row : rows) {
            if (KCDC::Shared::Game::FindItemClass(row.itemClass) == nullptr) {
                return Refuse("invalidItem");
            }
            if (row.amount == 0 || row.amount > kMaximumAmount) {
                return Refuse("invalidAmount");
            }
            if (const auto *custom = KCDC::Shared::Game::CustomItemRegistry::Get().Find(row.itemClass); custom != nullptr && !custom->stackable && row.amount != 1) {
                return Refuse("invalidAmount");
            }
            InventoryRow restored = row;
            std::string error;
            if (!Normalize(row.itemClass, row.metadata, restored.metadata, error)) {
                return Refuse(error);
            }
            if (restored.id.empty()) {
                do
                    restored.id = _ids.Next();
                while (given.contains(restored.id));
                given.insert(restored.id);
            }
            outputs.push_back({restored.id, restored.amount});
            edits.rows.insert_or_assign(restored.id, restored);
        }
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), "set", std::move(outputs));
    }

    InventoryResult Inventory::Wear(const std::string &id, const nlohmann::json &metadata) {
        const auto found = _rows.find(id);
        if (found == _rows.end()) {
            return Refuse("unknownItem");
        }
        InventoryRow row = found->second;
        std::string error;
        if (!Normalize(row.itemClass, metadata, row.metadata, error)) {
            return Refuse(error);
        }
        const auto before = Parse(row.itemClass, found->second.metadata);
        const auto after  = Parse(row.itemClass, row.metadata);
        // Only what use takes away: quality, health within a quality, and
        // poison charges (a spent coat clears its poison). Nothing may rise.
        const bool keepsPoison = after.poisonBuff == before.poisonBuff || (!after.poisonBuff.IsValid() && after.poisonCharges == 0);
        const bool noGain      = after.quality <= before.quality && (after.quality < before.quality || after.health <= before.health) && after.poisonCharges <= before.poisonCharges && after.onEquipBuffs == before.onEquipBuffs && keepsPoison;
        const bool lost        = after.quality < before.quality || after.health < before.health || after.poisonCharges < before.poisonCharges || after.poisonBuff != before.poisonBuff;
        if (!noGain || !lost) {
            return Refuse("invalidWear");
        }
        Edits edits;
        edits.rows.insert_or_assign(id, row);
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), "wear", {{id, row.amount}});
    }

    std::optional<std::string> Inventory::ReadRepairPart(const InventoryRepairPart &part, InventoryRow &row, float &before, float &after, nlohmann::json &metadata) const {
        const auto current = _rows.find(part.id);
        if (current == _rows.end()) {
            return "unknownItem";
        }
        row = current->second;
        nlohmann::json claimed;
        std::string error;
        if (!Normalize(row.itemClass, part.before, claimed, error) || !Normalize(row.itemClass, part.after, metadata, error)) {
            return error;
        }
        const auto was = Parse(row.itemClass, claimed);
        const auto now = Parse(row.itemClass, metadata);
        if (KCDC::Shared::Inventory::EncodeItemProperties(was) != KCDC::Shared::Inventory::EncodeItemProperties(Parse(row.itemClass, row.metadata))) {
            return "staleRepair";
        }
        // A repair moves health alone; the perks it may grant are corrected
        // on the owner, never recorded.
        if (now.quality != was.quality || now.poisonBuff != was.poisonBuff || now.poisonCharges != was.poisonCharges || now.onEquipBuffs != was.onEquipBuffs) {
            return "invalidRepair";
        }
        before = was.health;
        after  = now.health;
        return std::nullopt;
    }

    InventoryResult Inventory::Repair(const std::vector<InventoryRepairPart> &kits, const InventoryRepairPart &equipment, bool equipped) {
        auto part     = equipment;
        part.equipped = equipped;
        return Repair(kits, std::vector<InventoryRepairPart> {std::move(part)});
    }

    InventoryResult Inventory::Repair(const std::vector<InventoryRepairPart> &kits, const std::vector<InventoryRepairPart> &equipment) {
        if (kits.empty() || kits.size() > kMaximumRepairKits || equipment.empty() || equipment.size() > KCDC::Shared::Inventory::kMaximumRepairEquipment) {
            return Refuse("invalidRepair");
        }
        return RaiseHealth(kits, equipment, "repair");
    }

    InventoryResult Inventory::Sharpen(const InventoryRepairPart &weapon) {
        const auto row = _rows.find(weapon.id);
        if (row == _rows.end() || !KCDC::Shared::Game::IsSharpenableClass(row->second.itemClass)) {
            return Refuse("invalidSharpen");
        }
        return RaiseHealth({}, {weapon}, "sharpen");
    }

    InventoryResult Inventory::RaiseHealth(const std::vector<InventoryRepairPart> &kits, const std::vector<InventoryRepairPart> &equipment, const std::string &reason) {
        // Every claim names a unit from the starting inventory. Neither a
        // surviving kit nor a repaired item can fund another claim in this
        // selection, even when its output merges into another starting row.
        std::map<std::string, std::uint32_t> claimed;
        std::vector<InventoryRow> usedKits;
        std::vector<InventoryRow> repairedEquipment;
        std::map<std::string, std::vector<std::size_t>> selections;
        std::map<std::string, std::size_t> worn;
        const auto read = [&](const InventoryRepairPart &part, bool kit, InventoryRow &row) -> std::optional<std::string> {
            float before = 0.0F;
            float after  = 0.0F;
            nlohmann::json metadata;
            if (const auto error = ReadRepairPart(part, row, before, after, metadata)) {
                return error;
            }
            if (++claimed[row.id] > row.amount) {
                return "insufficientItems";
            }
            if (kit) {
                if (!KCDC::Shared::Game::IsRepairKitClass(row.itemClass) || before <= 0.0F || after > before || (!part.spent && after == before)) {
                    return "invalidRepair";
                }
                row.metadata = metadata;
            }
            else {
                const auto *definition = KCDC::Shared::Game::FindInventoryPropertyClass(row.itemClass);
                if (definition == nullptr || !definition->equippable || !definition->canHealthChange || after <= before || part.spent) {
                    return "invalidRepair";
                }
                // Script metadata stays; only repair health changes.
                row.metadata["health"]    = metadata.at("health");
                row.metadata["condition"] = metadata.at("condition");
            }
            row.amount = 1;
            return std::nullopt;
        };
        for (const auto &kit : kits) {
            InventoryRow row;
            if (const auto error = read(kit, true, row)) {
                return Refuse(*error);
            }
            usedKits.push_back(std::move(row));
        }
        for (std::size_t index = 0; index < equipment.size(); ++index) {
            const auto &part = equipment[index];
            InventoryRow row;
            if (const auto error = read(part, false, row)) {
                return Refuse(*error);
            }
            if (part.equipped && !worn.emplace(row.id, index).second) {
                return Refuse("invalidRepair");
            }
            selections[row.id].push_back(index);
            repairedEquipment.push_back(std::move(row));
        }
        Edits edits;
        std::vector<InventoryUnit> outputs;
        std::vector<InventoryCredit> credits;
        for (const auto &[id, amount] : claimed) {
            auto row = _rows.at(id);
            row.amount -= amount;
            edits.rows.insert_or_assign(id, row.amount != 0 ? std::optional<InventoryRow>(row) : std::nullopt);
        }
        for (std::size_t index = 0; index < kits.size(); ++index) {
            if (!kits[index].spent) {
                credits.push_back({usedKits[index].itemClass, 1, usedKits[index].metadata});
            }
        }
        for (const auto &[id, indices] : selections) {
            const auto &original = _rows.at(id);
            const auto untouched = original.amount - static_cast<std::uint32_t>(indices.size());
            // Keep the worn copy's row regardless of selection order. With
            // no copies left, keep one repaired unit in the original row.
            const auto held                       = worn.find(id);
            const std::optional<std::size_t> keep = held != worn.end() ? std::optional<std::size_t>(held->second) : untouched == 0 ? std::optional<std::size_t>(indices.back()) : std::nullopt;
            if (keep) {
                edits.rows.insert_or_assign(id, repairedEquipment[*keep]);
                outputs.push_back({id, 1});
                if (untouched != 0) {
                    credits.push_back({original.itemClass, untouched, original.metadata});
                }
            }
            for (const auto index : indices) {
                if (!keep || index != *keep) {
                    credits.push_back({repairedEquipment[index].itemClass, 1, repairedEquipment[index].metadata});
                }
            }
        }
        for (const auto &credit : credits) {
            outputs.push_back({Credit(credit.itemClass, credit.amount, credit.metadata, edits), credit.amount});
        }
        if (const auto full = CheckCapacity(edits)) {
            return Refuse(*full);
        }
        return Commit(std::move(edits), reason, std::move(outputs));
    }

    nlohmann::json Inventory::EncodeRow(const InventoryRow &row) {
        const auto *definition = KCDC::Shared::Game::FindItemClass(row.itemClass);
        return {{"id", row.id}, {"item", row.itemClass}, {"name", definition == nullptr ? std::string() : std::string(definition->name)}, {"amount", row.amount}, {"quality", row.metadata.value("quality", 0U)}, {"health", row.metadata.value("health", 0.0)},
            {"condition", row.metadata.value("condition", 0.0)}, {"metadata", row.metadata}};
    }

    nlohmann::json Inventory::EncodeChange(const InventoryChange &change) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto &row : change.rows) {
            rows.push_back({{"id", row.id}, {"before", row.before ? EncodeRow(*row.before) : nlohmann::json(nullptr)}, {"after", row.after ? EncodeRow(*row.after) : nlohmann::json(nullptr)}});
        }
        return {{"revision", change.revision}, {"reason", change.reason}, {"items", std::move(rows)}};
    }

    InventoryResult Inventory::Transfer(Inventory &source, Inventory &target, const std::vector<InventoryUnit> &units, std::optional<std::uint64_t> sourceRevision, std::optional<std::uint64_t> targetRevision, const std::string &reason, bool notify) {
        if (&source == &target) {
            return Refuse("sameInventory");
        }
        if (source.IsStale(sourceRevision) || target.IsStale(targetRevision)) {
            return Refuse("staleRevision");
        }
        Edits debits;
        if (const auto invalid = source.CheckUnits(units, debits)) {
            return Refuse(*invalid);
        }
        source.Debit(units, debits);
        Edits credits;
        std::vector<InventoryUnit> outputs;
        for (const auto &unit : units) {
            const auto &row = source._rows.at(unit.id);
            target.CreditUnits(row.itemClass, unit.amount, row.metadata, credits, outputs);
        }
        if (const auto full = target.CheckCapacity(credits)) {
            return Refuse(*full);
        }
        const auto moved = source.Commit(std::move(debits), reason, {}, notify);
        target.Commit(std::move(credits), reason, {}, notify);
        return {true, "", moved.revision, std::move(outputs)};
    }
} // namespace KCDC::Features::Inventory
