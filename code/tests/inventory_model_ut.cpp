#include "features/inventory/inventory.h"
#include "shared/inventory_property_policy.h"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    constexpr const char *kSword = "ce1953df-9f10-4138-8fb1-393a11474553";
    constexpr const char *kSage  = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kArrow = "dfea5d01-b25c-414a-9ab4-6911a5f82118";

    void RequireModel(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    nlohmann::json Canonical(const char *item, const nlohmann::json &metadata) {
        nlohmann::json result;
        std::string error;
        RequireModel(KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(item, metadata, result, error), "fixture metadata normalizes");
        return result;
    }

    std::uint32_t Amount(const KCDC::Features::Inventory::Inventory &inventory, const std::string &id) {
        const auto found = inventory.Rows().find(id);
        return found == inventory.Rows().end() ? 0 : found->second.amount;
    }

    void AddMergesAndRecords() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        RequireModel(inventory.Revision() == 0 && inventory.Rows().empty(), "a new inventory is empty at revision zero");

        const auto first = inventory.Add(kSage, 3, nlohmann::json::object(), std::nullopt);
        RequireModel(first.ok && first.code.empty() && first.revision == 1 && first.outputs.size() == 1 && first.outputs[0].amount == 3, "a grant creates one row and advances the revision");
        const auto row = first.outputs[0].id;
        RequireModel(inventory.Rows().at(row).metadata == Canonical(kSage, nlohmann::json::object()), "a grant stores canonical properties, not the request");

        const auto second = inventory.Add(kSage, 2, {{"quality", 1}}, std::nullopt);
        RequireModel(second.ok && second.outputs.size() == 1 && second.outputs[0].id == row && Amount(inventory, row) == 5 && inventory.Rows().size() == 1, "equal class and properties merge into the existing row");

        const auto worn = inventory.Add(kSage, 1, {{"health", 0.5}}, std::nullopt);
        RequireModel(worn.ok && worn.outputs[0].id != row && inventory.Rows().size() == 2, "different properties keep their own row");

        const auto changes = inventory.TakeChanges();
        RequireModel(changes.size() == 3 && changes[0].reason == "add" && changes[0].revision == 1, "every successful operation records one change in order");
        RequireModel(!changes[0].rows[0].before && changes[0].rows[0].after && changes[0].rows[0].after->amount == 3, "a created row has no before");
        RequireModel(changes[1].rows[0].before->amount == 3 && changes[1].rows[0].after->amount == 5, "a merged row reports both amounts");
        RequireModel(inventory.TakeChanges().empty(), "changes are drained once");
    }

    void AddRefusesInvalidRequests() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        RequireModel(inventory.Add("00000000-0000-0000-0000-000000000001", 1, nlohmann::json::object(), std::nullopt).code == "invalidItem", "an unknown class is refused");
        RequireModel(inventory.Add(kSage, 0, nlohmann::json::object(), std::nullopt).code == "invalidAmount", "zero is not an amount");
        RequireModel(inventory.Add(kSword, 1, {{"quality", 9}}, std::nullopt).code == "invalidQuality", "property policy errors are returned as they are");
        RequireModel(inventory.Add(kSword, 1, {{"ownerId", 5}}, std::nullopt).code == "invalidMetadata", "provenance is not an inventory property");
        RequireModel(inventory.Add(kSage, 1, nlohmann::json::object(), 7).code == "staleRevision", "an expected revision that moved on is refused");
        RequireModel(inventory.Revision() == 0 && inventory.Rows().empty() && inventory.TakeChanges().empty(), "refusals change nothing and record nothing");
    }

    void RemoveIsAllOrNothing() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage  = inventory.Add(kSage, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto sword = inventory.Add(kSword, 1, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();

        RequireModel(inventory.Remove({{sage, 2}, {sword, 2}}, std::nullopt).code == "insufficientItems", "asking for more than a row holds is refused");
        RequireModel(Amount(inventory, sage) == 3 && Amount(inventory, sword) == 1, "a refused removal takes nothing from any row");
        RequireModel(inventory.Remove({{"missing", 1}}, std::nullopt).code == "unknownItem", "an unknown row is refused");
        RequireModel(inventory.Remove({{sage, 1}, {sage, 1}}, std::nullopt).code == "invalidItems", "a row named twice is refused");
        RequireModel(inventory.Remove({}, std::nullopt).code == "invalidItems", "an empty removal is refused");
        RequireModel(inventory.Remove({{sage, 0}}, std::nullopt).code == "invalidAmount", "a zero unit is refused");

        const auto removed = inventory.Remove({{sage, 2}, {sword, 1}}, 2);
        RequireModel(removed.ok && removed.revision == 3 && Amount(inventory, sage) == 1 && !inventory.Rows().contains(sword), "a removal spends exact units and deletes emptied rows");
        const auto changes = inventory.TakeChanges();
        RequireModel(changes.size() == 1 && changes[0].rows.size() == 2, "one removal is one change covering every row it touched");
    }

    void TakeSpendsAcrossRows() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        inventory.Add(kSage, 2, nlohmann::json::object(), std::nullopt);
        inventory.Add(kSage, 2, {{"health", 0.5}}, std::nullopt);
        RequireModel(inventory.Take(kSage, 5, std::nullopt).code == "insufficientItems" && inventory.Revision() == 2, "a take that cannot be met removes nothing");
        const auto taken = inventory.Take(kSage, 3, std::nullopt);
        RequireModel(taken.ok && inventory.Rows().size() == 1 && inventory.Rows().begin()->second.amount == 1, "a take spends across rows of the class");
    }

    void SetPropertiesKeepsIdentity() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sword = inventory.Add(kSword, 1, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto set   = inventory.SetProperties(sword, {{"quality", 2}, {"condition", 0.5}}, std::nullopt);
        RequireModel(set.ok && inventory.Rows().at(sword).metadata == Canonical(kSword, {{"quality", 2}, {"condition", 0.5}}) && Amount(inventory, sword) == 1, "properties are replaced, identity and amount kept");
        RequireModel(inventory.SetProperties(sword, {{"quality", 9}}, std::nullopt).code == "invalidQuality", "the policy guards property writes too");
        RequireModel(inventory.SetProperties("missing", nlohmann::json::object(), std::nullopt).code == "unknownItem", "an unknown row is refused");
    }

    void TransferMovesUnits() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory source(ids), target(ids);
        const auto arrows = source.Add(kArrow, 10, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto held   = target.Add(kArrow, 1, nlohmann::json::object(), std::nullopt).outputs[0].id;
        source.TakeChanges();
        target.TakeChanges();

        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(source, source, {{arrows, 1}}, std::nullopt, std::nullopt).code == "sameInventory", "an inventory cannot trade with itself");
        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(source, target, {{arrows, 11}}, std::nullopt, std::nullopt).code == "insufficientItems", "a transfer cannot move more than is held");
        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(source, target, {{arrows, 1}}, std::nullopt, 9).code == "staleRevision", "either side's expected revision is checked");

        const auto moved = KCDC::Features::Inventory::Inventory::Transfer(source, target, {{arrows, 4}}, 1, 1);
        RequireModel(moved.ok && Amount(source, arrows) == 6 && Amount(target, held) == 5 && moved.outputs.size() == 1 && moved.outputs[0].id == held, "moved units merge into the matching target row");
        const auto sourceChanges = source.TakeChanges();
        const auto targetChanges = target.TakeChanges();
        RequireModel(sourceChanges.size() == 1 && targetChanges.size() == 1 && sourceChanges[0].reason == "transfer" && targetChanges[0].reason == "transfer", "both sides record one transfer change");
    }

    void ExchangeIsOneChange() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage   = inventory.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto arrows = inventory.Add(kArrow, 10, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();

        const auto traded = inventory.Exchange({{sage, 2}, {arrows, 10}}, {{kSword, 1}, {kArrow, 3}}, 2, "trade");
        RequireModel(traded.ok && traded.revision == 3 && Amount(inventory, sage) == 3 && Amount(inventory, arrows) == 0 && traded.outputs.size() == 2 && Amount(inventory, traded.outputs[0].id) == 1, "an exchange debits and credits in one commit");
        RequireModel(traded.outputs[1].id != arrows && Amount(inventory, traded.outputs[1].id) == 3, "a credit never lands in a row the same exchange emptied");
        const auto changes = inventory.TakeChanges();
        RequireModel(changes.size() == 1 && changes[0].reason == "trade" && changes[0].rows.size() == 4, "an exchange records one change with its reason");

        const auto before = inventory.Rows();
        RequireModel(inventory.Exchange({{sage, 4}}, {{kSword, 1}}, std::nullopt, "trade").code == "insufficientItems" && inventory.Rows() == before, "a debit that cannot be paid moves nothing");
        RequireModel(inventory.Exchange({{sage, 1}}, {{"not-an-item", 1}}, std::nullopt, "trade").code == "invalidItem" && inventory.Rows() == before, "a credit that cannot be granted moves nothing");
        RequireModel(inventory.Exchange({{sage, 1}}, {}, 2, "trade").code == "staleRevision", "an exchange checks the expected revision");
        RequireModel(inventory.Exchange({}, {}, std::nullopt, "trade").code == "invalidItems" && inventory.TakeChanges().empty(), "an empty exchange is refused");
    }

    void ExchangeIsAtomicOnCapacity() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids, 2);
        const auto sage = inventory.Add(kSage, 2, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.Add(kArrow, 1, nlohmann::json::object(), std::nullopt);
        const auto before = inventory.Rows();
        RequireModel(inventory.Exchange({{sage, 1}}, {{kSword, 1}}, std::nullopt, "trade").code == "inventoryCapacity" && inventory.Rows() == before, "an exchange that overflows moves nothing");
        RequireModel(inventory.Exchange({{sage, 2}}, {{kSword, 1}}, std::nullopt, "trade").ok, "a row the exchange empties makes room for what it credits");
    }

    void TransferIsAtomicOnCapacity() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory source(ids), target(ids);
        KCDC::Features::Inventory::InventoryResult filled;
        nlohmann::json refused;
        do {
            refused = {{"health", 0.01 + (static_cast<double>(target.Rows().size()) / 5000.0)}};
            filled  = target.Add(kSage, 1, refused, std::nullopt);
        } while (filled.ok);
        RequireModel(filled.code == "inventoryCapacity" && target.Rows().size() <= KCDC::Features::Inventory::Inventory::kMaximumRows, "a full inventory refuses a new row");
        // The very row the target just refused, offered through a transfer.
        const auto twin = source.Add(kSage, 1, refused, std::nullopt).outputs[0].id;
        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(source, target, {{twin, 1}}, std::nullopt, std::nullopt).code == "inventoryCapacity" && Amount(source, twin) == 1, "a transfer the target cannot hold leaves the source untouched");
    }

    void TransferNamesItsReason() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory source(ids), target(ids);
        const auto arrows = source.Add(kArrow, 2, nlohmann::json::object(), std::nullopt).outputs[0].id;
        source.TakeChanges();
        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(source, target, {{arrows, 1}}, std::nullopt, std::nullopt, "deposit").ok, "a named transfer moves units");
        const auto sourceChanges = source.TakeChanges();
        const auto targetChanges = target.TakeChanges();
        RequireModel(sourceChanges.size() == 1 && targetChanges.size() == 1 && sourceChanges[0].reason == "deposit" && targetChanges[0].reason == "deposit", "both sides record the transfer's reason");
    }

    void NoticesFollowOnlyAnnouncedChanges() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids), other(ids);
        const auto sage = inventory.Add(kSage, 4, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireModel(inventory.TakeNotices().empty(), "a change is silent unless its cause asks to be announced");

        RequireModel(inventory.Add(kSage, 8, nlohmann::json::object(), std::nullopt, "gather", true).ok, "an announced gain applies");
        RequireModel(inventory.Remove({{sage, 3}}, std::nullopt, "remove", true).ok, "an announced loss applies");
        RequireModel(inventory.Take(kSage, 1, std::nullopt, "use").ok, "a silent spend applies");
        const auto notices = inventory.TakeNotices();
        RequireModel(notices.size() == 1 && notices.at(kSage) == 5, "announced changes sum per class and silent ones are left out");
        RequireModel(inventory.TakeNotices().empty(), "notices are drained once");

        RequireModel(inventory.Add(kSage, 2, nlohmann::json::object(), std::nullopt, "add", true).ok && inventory.Remove({{sage, 2}}, std::nullopt, "remove", true).ok, "a gain and an equal loss apply");
        RequireModel(inventory.TakeNotices().empty(), "a gain undone before the next sync is no news");

        const auto arrows = inventory.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireModel(KCDC::Features::Inventory::Inventory::Transfer(inventory, other, {{arrows, 2}}, std::nullopt, std::nullopt, "transfer", true).ok, "an announced transfer applies");
        RequireModel(inventory.TakeNotices() == std::map<std::string, std::int64_t> {{kArrow, -2}} && other.TakeNotices() == std::map<std::string, std::int64_t> {{kArrow, 2}}, "both sides of a transfer announce their own side");

        const auto rows = inventory.Rows();
        std::vector<KCDC::Features::Inventory::InventoryRow> restored;
        for (const auto &[id, row] : rows) {
            restored.push_back(row);
        }
        restored.push_back({"", kSword, 1, nlohmann::json::object()});
        RequireModel(inventory.Set(restored, std::nullopt).ok && inventory.TakeNotices().empty(), "a restore is never announced");
    }

    void RowLimitIsPerInventory() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory chest(ids, 2);
        RequireModel(chest.Add(kArrow, 1, nlohmann::json::object(), std::nullopt).ok && chest.Add(kSage, 1, nlohmann::json::object(), std::nullopt).ok, "rows up to the limit are accepted");
        RequireModel(chest.Add(kSword, 1, nlohmann::json::object(), std::nullopt).code == "inventoryCapacity" && chest.Rows().size() == 2, "a row past this inventory's limit is refused");
        RequireModel(chest.Add(kArrow, 4, nlohmann::json::object(), std::nullopt).ok, "a merge needs no new row");
    }

    void SetRestoresSavedRows() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto old = inventory.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();

        std::vector<KCDC::Features::Inventory::InventoryRow> saved = {{"saved-sword", kSword, 1, {{"quality", 2}, {"condition", 0.5}}}, {"", kSage, 4, nlohmann::json::object()}};
        const auto restored                                        = inventory.Set(saved, std::nullopt);
        RequireModel(restored.ok && inventory.Rows().size() == 2 && inventory.Rows().contains("saved-sword") && !inventory.Rows().contains(old), "a restore replaces every row and keeps saved identities");
        RequireModel(inventory.Rows().at("saved-sword").metadata == Canonical(kSword, {{"quality", 2}, {"condition", 0.5}}), "restored properties are normalized");
        const auto changes = inventory.TakeChanges();
        RequireModel(changes.size() == 1 && changes[0].reason == "set" && changes[0].rows.size() == 3, "a restore is one change covering removed and created rows");

        RequireModel(inventory.Set({{"a", kSage, 1, nlohmann::json::object()}, {"a", kSage, 1, nlohmann::json::object()}}, std::nullopt).code == "invalidItems", "duplicate saved identities are refused");
        RequireModel(inventory.Set({{"b", kSword, 1, {{"quality", 9}}}}, std::nullopt).code == "invalidQuality", "one invalid saved row refuses the whole restore");
        RequireModel(inventory.Set({{std::string(65, 'x'), kSage, 1, nlohmann::json::object()}}, std::nullopt).code == "invalidItems", "an oversized identity is refused");
        RequireModel(inventory.Rows().size() == 2, "a refused restore changes nothing");
        RequireModel(inventory.Set({}, std::nullopt).ok && inventory.Rows().empty(), "an empty restore clears the inventory");
    }

    void GeneratedIdentitiesAvoidRestoredOnes() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto next = ids.Peek();
        RequireModel(inventory.Set({{next, kSword, 1, nlohmann::json::object()}}, std::nullopt).ok, "a saved identity may equal the next generated one");
        const auto added = inventory.Add(kSage, 1, nlohmann::json::object(), std::nullopt);
        RequireModel(added.ok && added.outputs[0].id != next && inventory.Rows().size() == 2, "a generated identity never replaces a restored row");
    }

    void WearOnlyLowers() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sword = inventory.Add(kSword, 1, {{"quality", 3}, {"condition", 0.8}}, std::nullopt).outputs[0].id;
        inventory.TakeChanges();

        const auto worn = inventory.Wear(sword, {{"quality", 3}, {"condition", 0.5}});
        RequireModel(worn.ok && inventory.Rows().at(sword).metadata == Canonical(kSword, {{"quality", 3}, {"condition", 0.5}}), "lower condition is accepted");
        RequireModel(inventory.Wear(sword, {{"quality", 2}, {"condition", 1.0}}).ok, "a quality drop is wear even when the new band starts full");
        RequireModel(inventory.Wear(sword, {{"quality", 3}, {"condition", 0.1}}).code == "invalidWear", "quality never rises");
        RequireModel(inventory.Wear(sword, {{"quality", 2}, {"condition", 1.0}}).code == "invalidWear", "an unchanged state is not wear");
        RequireModel(inventory.Wear("missing", {{"quality", 1}}).code == "unknownItem", "wear names a held row");
        const auto changes = inventory.TakeChanges();
        RequireModel(changes.size() == 2 && changes[0].reason == "wear", "accepted wear is a change like any other");
    }

    constexpr const char *kRepairKit = "c707733a-c0a7-4f02-b684-9392b0b15b83";

    KCDC::Features::Inventory::InventoryRepairPart RepairPart(const KCDC::Features::Inventory::Inventory &inventory, const std::string &id, const nlohmann::json &after, bool spent = false) {
        return {id, inventory.Rows().at(id).metadata, after, spent};
    }

    void RepairCommitsCostAndGain() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto kitId = inventory.Add(kRepairKit, 1, {{"health", 0.8}}, std::nullopt).outputs[0].id;
        const auto sword = inventory.Add(kSword, 1, {{"quality", 3}, {"condition", 0.5}}, std::nullopt).outputs[0].id;
        const auto kit   = RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.6}});
        const auto item  = RepairPart(inventory, sword, {{"quality", 3}, {"condition", 1.0}});
        inventory.TakeChanges();
        const auto revision = inventory.Revision();
        RequireModel(inventory.Repair({kit}, item, true).ok, "native kit repair is accepted");
        std::uint32_t kits = 0;
        for (const auto &[id, row] : inventory.Rows()) {
            if (row.itemClass == kRepairKit) {
                kits += row.amount;
                RequireModel(row.metadata == Canonical(kRepairKit, kit.after), "repair keeps the kit's durability cost");
            }
        }
        RequireModel(kits == 1, "a kit that lasts stays one kit");
        RequireModel(inventory.Rows().at(sword).metadata == Canonical(kSword, item.after), "repair raises equipment condition persistently");
        const auto changes = inventory.TakeChanges();
        RequireModel(inventory.Revision() == revision + 1 && changes.size() == 1 && changes[0].reason == "repair", "cost and gain are one revision and one script event");
        const auto rows = inventory.Rows();
        RequireModel(!inventory.Repair({kit}, item, true).ok && inventory.Rows() == rows, "replayed repair cannot charge a kit or heal again");
    }

    void RepairSpendsSeveralKitsOfOneRow() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto kitId = inventory.Add(kRepairKit, 3, {{"health", 0.2}}, std::nullopt).outputs[0].id;
        const auto sword = inventory.Add(kSword, 1, {{"quality", 2}, {"condition", 0.1}}, std::nullopt).outputs[0].id;
        // CombineItems empties one kit, splits the next off, and stops when
        // the second still has some left.
        const std::vector<KCDC::Features::Inventory::InventoryRepairPart> kits {RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.0}}, true), RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.1}})};
        const auto item = RepairPart(inventory, sword, {{"quality", 2}, {"condition", 0.9}});
        RequireModel(inventory.Repair(kits, item, false).ok, "one combine may spend several kits of one row");
        std::uint32_t full = 0, used = 0;
        for (const auto &[id, row] : inventory.Rows()) {
            if (row.itemClass != kRepairKit) {
                continue;
            }
            if (row.metadata == Canonical(kRepairKit, {{"health", 0.2}})) {
                full += row.amount;
            }
            else if (row.metadata == Canonical(kRepairKit, {{"health", 0.1}})) {
                used += row.amount;
            }
        }
        RequireModel(Amount(inventory, kitId) == 1 && full == 1 && used == 1, "the emptied kit is gone, the used one has its own row and the untouched one keeps its row");
        const std::vector<KCDC::Features::Inventory::InventoryRepairPart> twice {RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.0}}, true), RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.0}}, true)};
        const auto rows = inventory.Rows();
        RequireModel(!inventory.Repair(twice, RepairPart(inventory, sword, {{"quality", 2}, {"condition", 1.0}}), false).ok && inventory.Rows() == rows, "a row cannot pay for more kits than it holds");
    }

    void RepairKeepsTheWornCopysRow() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto kitId   = inventory.Add(kRepairKit, 2, {{"health", 0.8}}, std::nullopt).outputs[0].id;
        const auto sword   = inventory.Add(kSword, 3, {{"quality", 2}, {"condition", 0.5}}, std::nullopt).outputs[0].id;
        const auto damaged = inventory.Rows().at(sword).metadata;
        const auto kit     = RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.6}});
        const auto item    = RepairPart(inventory, sword, {{"quality", 2}, {"condition", 0.7}});
        auto loose         = inventory;
        RequireModel(inventory.Repair({kit}, item, true).ok, "a worn copy can be repaired out of its stack");
        RequireModel(Amount(inventory, sword) == 1 && inventory.Rows().at(sword).metadata == Canonical(kSword, item.after), "the worn copy keeps its row, so the equipment record still names it");
        RequireModel(loose.Repair({kit}, item, false).ok, "a loose copy can be repaired out of its stack");
        RequireModel(Amount(loose, sword) == 2 && loose.Rows().at(sword).metadata == damaged, "a loose copy moves out, so the row the body wears keeps its condition");
        std::uint32_t untouched = 0;
        for (const auto &[id, row] : inventory.Rows()) {
            if (row.itemClass == kSword && id != sword) {
                untouched += row.amount;
                RequireModel(row.metadata == damaged, "unselected copies keep their damaged condition");
            }
        }
        RequireModel(untouched == 2, "every untouched copy is kept");
    }

    void RepairRefusalsAreAtomic() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto kitId = inventory.Add(kRepairKit, 1, {{"health", 0.8}}, std::nullopt).outputs[0].id;
        const auto sword = inventory.Add(kSword, 1, {{"quality", 2}, {"condition", 0.5}}, std::nullopt).outputs[0].id;
        const auto herb  = inventory.Add(kSage, 1, {{"health", 0.8}}, std::nullopt).outputs[0].id;
        const auto kit   = RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.6}});
        const auto item  = RepairPart(inventory, sword, {{"quality", 2}, {"condition", 1.0}});
        inventory.TakeChanges();
        const auto rows     = inventory.Rows();
        const auto revision = inventory.Revision();
        const auto refuse   = [&](const KCDC::Features::Inventory::InventoryRepairPart &badKit, const KCDC::Features::Inventory::InventoryRepairPart &badItem) {
            RequireModel(!inventory.Repair({badKit}, badItem, true).ok, "invalid repair is refused");
            RequireModel(inventory.Rows() == rows && inventory.Revision() == revision && inventory.TakeChanges().empty(), "refusal changes neither kit, equipment, revision nor events");
        };
        auto bad  = kit;
        bad.after = bad.before;
        refuse(bad, item); // no durability cost
        bad       = kit;
        bad.spent = true;
        bad.after = {{"quality", 1}, {"health", 0.9}};
        refuse(bad, item); // spending a kit cannot raise its durability
        bad    = kit;
        bad.id = herb;
        refuse(bad, item); // food and herbs cannot pay for repair
        bad        = kit;
        bad.before = Canonical(kRepairKit, {{"health", 0.9}});
        refuse(bad, item); // stale starting state
        bad                  = item;
        bad.after["quality"] = 3;
        refuse(kit, bad); // repairs cannot upgrade quality
        bad       = item;
        bad.after = bad.before;
        refuse(kit, bad); // no equipment gain
        bad                    = item;
        bad.after["condition"] = 1.5;
        refuse(kit, bad); // condition remains bounded by class policy
        bad    = item;
        bad.id = "missing";
        refuse(kit, bad); // equipment unit must exist
        RequireModel(!inventory.Repair({}, item, true).ok, "a repair spends a kit");
        RequireModel(inventory.Wear(sword, item.after).code == "invalidWear", "ordinary wear still refuses repairs without a kit");
    }

    void RepairIsAtomicOnCapacity() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids, 2);
        const auto kitId    = inventory.Add(kRepairKit, 1, {{"health", 0.8}}, std::nullopt).outputs[0].id;
        const auto sword    = inventory.Add(kSword, 2, {{"quality", 2}, {"condition", 0.5}}, std::nullopt).outputs[0].id;
        const auto kit      = RepairPart(inventory, kitId, {{"quality", 1}, {"health", 0.6}});
        const auto item     = RepairPart(inventory, sword, {{"quality", 2}, {"condition", 1.0}});
        const auto rows     = inventory.Rows();
        const auto revision = inventory.Revision();
        RequireModel(!inventory.Repair({kit}, item, true).ok && inventory.Rows() == rows && inventory.Revision() == revision, "a repair split exceeding capacity charges nothing");
    }

    void RowsStayWithinTheWire() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        RequireModel(inventory.Add(kSage, KCDC::Features::Inventory::Inventory::kMaximumAmount, nlohmann::json::object(), std::nullopt).ok, "a full stack fits one row");
        const auto overflow = inventory.Add(kSage, 1, nlohmann::json::object(), std::nullopt);
        RequireModel(overflow.ok && inventory.Rows().size() == 2, "a merge that would overflow a row opens a new one");
    }
} // namespace

void InventoryModelTests() {
    AddMergesAndRecords();
    AddRefusesInvalidRequests();
    RemoveIsAllOrNothing();
    TakeSpendsAcrossRows();
    SetPropertiesKeepsIdentity();
    TransferMovesUnits();
    TransferIsAtomicOnCapacity();
    ExchangeIsOneChange();
    ExchangeIsAtomicOnCapacity();
    TransferNamesItsReason();
    NoticesFollowOnlyAnnouncedChanges();
    RowLimitIsPerInventory();
    SetRestoresSavedRows();
    GeneratedIdentitiesAvoidRestoredOnes();
    WearOnlyLowers();
    RepairCommitsCostAndGain();
    RepairSpendsSeveralKitsOfOneRow();
    RepairKeepsTheWornCopysRow();
    RepairRefusalsAreAtomic();
    RepairIsAtomicOnCapacity();
    RowsStayWithinTheWire();
}
