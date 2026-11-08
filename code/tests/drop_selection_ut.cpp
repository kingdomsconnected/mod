#include "features/ground_item/ground_stock.h"
#include "shared/features/ground_item/drop_selection.h"

#include <stdexcept>

namespace {
    constexpr const char *kSage  = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kArrow = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
    constexpr const char *kTorch = "4cea28a0-0814-405a-bf24-4fd711f7eb63";

    void RequireDrop(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void OneSelectionSendsOnePayload() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto row = inventory.Add(kSage, 14, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();
        const auto revision = inventory.Revision();
        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        for (int i = 0; i < 14; ++i) {
            selection.Add(row, 1, false);
        }
        const auto payload = selection.Finish();
        RequireDrop(payload.has_value() && !selection.Finish(), "fourteen native callbacks yield exactly one payload");
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*payload, units) && units.size() == 1 && units[0].amount == 14, "one row split over native stacks is combined before sending");
        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(inventory, units);
        RequireDrop(dropped.result.ok && inventory.Rows().empty() && dropped.stacks.size() == 1 && dropped.stacks[0].item.amount == 14, "drop all removes all fourteen units together");
        RequireDrop(inventory.Revision() == revision + 1 && inventory.TakeChanges().size() == 1, "one selection commits one inventory mutation");
    }

    void PartialSelectionPreservesTheRemainderAndNextAction() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto row = inventory.Add(kSage, 14, nlohmann::json::object(), std::nullopt).outputs[0].id;
        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        selection.Add(row, 2, false);
        selection.Add(row, 3, false);
        const auto payload = selection.Finish();
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(payload && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*payload, units), "partial selection decodes");
        RequireDrop(KCDC::Features::GroundItem::GroundStock::Drop(inventory, units).result.ok && inventory.Rows().at(row).amount == 9, "only the selected five units leave");
        selection.Begin();
        selection.Add(row, 1, false);
        const auto next = selection.Finish();
        RequireDrop(next && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*next, units) && units[0].amount == 1, "a new click does not inherit the previous selection");
    }

    void SeveralRowsAreValidatedTogether() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage   = inventory.Add(kSage, 7, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto arrows = inventory.Add(kArrow, 7, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();
        const auto revision = inventory.Revision();
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 7}, {arrows, 8}}).result.ok, "an unavailable amount rejects the batch");
        RequireDrop(inventory.Revision() == revision && inventory.Rows().at(sage).amount == 7 && inventory.TakeChanges().empty(), "a bad later row does not remove the earlier rows");
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 7}, {"missing", 1}}).result.ok && inventory.Revision() == revision, "a stale identity also leaves the entire selection unchanged");

        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        selection.Add(sage, 7, false);
        selection.Add(arrows, 7, true);
        const auto payload = selection.Finish();
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(payload && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*payload, units), "several rows share one payload");
        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(inventory, units);
        RequireDrop(dropped.result.ok && inventory.Rows().empty() && dropped.stacks.size() == 2, "a valid batch removes all its rows");
        RequireDrop(!dropped.stacks[0].retiresHeldItem && dropped.stacks[1].retiresHeldItem, "only the selected held row retires a hand");
    }

    void SelectionBoundsNeverSendAPartialBatch() {
        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        for (std::size_t i = 0; i < KCDC::Shared::Inventory::kMaximumDropUnits; ++i) {
            selection.Add(std::to_string(i), 1, false);
        }
        const auto full = selection.Finish();
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(full && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*full, units) && units.size() == 64, "a batch can name the entire ground allowance");
        selection.Begin();
        for (std::size_t i = 0; i <= KCDC::Shared::Inventory::kMaximumDropUnits; ++i) {
            selection.Add(std::to_string(i), 1, false);
        }
        RequireDrop(!selection.Finish() && !selection.IsValid(), "an oversized selection sends none of its rows");
        selection.Begin();
        selection.Add("1", KCDC::Shared::RPC::kMaxInventoryStackAmount, false);
        selection.Add("1", 1, false);
        RequireDrop(!selection.Finish(), "coalescing cannot overflow an amount");
        selection.Begin();
        selection.Add("1", 1, false);
        selection.Refuse();
        RequireDrop(!selection.Finish(), "an unmapped later native stack cancels the whole selection");
        selection.Begin();
        RequireDrop(!selection.Finish() && selection.IsValid(), "an empty selection sends nothing and clears an earlier refusal");
    }

    void NestedSelectionAndSingleDropsFinishOnce() {
        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        selection.Add("1", 1, false);
        selection.Begin();
        selection.Add("1", 2, true);
        RequireDrop(!selection.Finish() && selection.IsActive(), "nested callbacks do not flush the outer action");
        const auto payload = selection.Finish();
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(payload && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*payload, units) && units.size() == 1 && units[0].amount == 3 && units[0].held, "coalescing retains the held marker");
        selection.Begin();
        selection.Add("2", 14, false);
        RequireDrop(selection.Finish().has_value(), "a single native stack also sends one payload");
    }

    void TorchesSplitAfterOnePayloadAndRoundTrip() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto row = inventory.Add(kTorch, 14, {{"condition", 0.5}}, std::nullopt).outputs[0].id;
        const auto properties = inventory.Rows().at(row).metadata;
        inventory.TakeChanges();
        const auto revision = inventory.Revision();
        KCDC::Shared::Inventory::DropSelection selection;
        selection.Begin();
        for (int index = 0; index < 14; ++index) {
            selection.Add(row, 1, index == 0);
        }
        const auto payload = selection.Finish();
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(payload && !selection.Finish() && KCDC::Features::GroundItem::GroundStock::ReadDropRequest(*payload, units) && units.size() == 1, "fourteen torches still send exactly one payload and row");
        const auto drop = KCDC::Features::GroundItem::GroundStock::Drop(inventory, units, 14);
        RequireDrop(drop.result.ok && drop.stacks.size() == 14 && inventory.Rows().empty(), "fourteen non-stackable torches become fourteen ground items");
        RequireDrop(inventory.Revision() == revision + 1 && inventory.TakeChanges().size() == 1, "splitting torches commits only one inventory change");
        unsigned held = 0;
        for (const auto &stack : drop.stacks) {
            RequireDrop(stack.item.amount == 1 && KCDC::Features::GroundItem::GroundStock::CanPlace(stack.item), "every torch can materialize as one native item");
            held += stack.retiresHeldItem;
            RequireDrop(KCDC::Features::GroundItem::GroundStock::Pickup(inventory, stack.item).ok, "every dropped torch can be picked up");
        }
        RequireDrop(held == 1, "only one torch retires the held shadow");
        RequireDrop(inventory.Rows().size() == 1 && inventory.Rows().begin()->second.amount == 14 && inventory.Rows().begin()->second.metadata == properties, "picking up all torches preserves quantity and properties");
        auto invalid = drop.stacks.front().item;
        invalid.amount = 14;
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::CanPlace(invalid), "script spawning cannot bypass native stackability");
    }

    void PhysicalStackCapacityIsAtomic() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto torch = inventory.Add(kTorch, 65, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto arrow = inventory.Add(kArrow, 14, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();
        const auto revision = inventory.Revision();
        RequireDrop(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{torch, 14}}, 13).result.code == "groundCapacity", "a torch row needs fourteen free ground slots");
        RequireDrop(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrow, 14}, {torch, 14}}, 14).result.code == "groundCapacity", "mixed selections count one arrow stack plus fourteen torches");
        RequireDrop(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{torch, 65}}).result.code == "groundCapacity", "one non-stackable row cannot exceed the entire ground allowance");
        RequireDrop(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{torch, 1}}, 0).result.code == "groundCapacity", "no free slots means no removal");
        RequireDrop(inventory.Revision() == revision && inventory.TakeChanges().empty() && inventory.Rows().at(torch).amount == 65 && inventory.Rows().at(arrow).amount == 14, "capacity refusal preserves all rows and their revision");
        const auto partial = KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{torch, 5, true}}, 5);
        RequireDrop(partial.result.ok && partial.stacks.size() == 5 && inventory.Rows().at(torch).amount == 60, "a partial torch selection drops exactly five");
        RequireDrop(partial.stacks.front().retiresHeldItem && !partial.stacks.back().retiresHeldItem, "a held single torch retires even when other torches remain in the row");
        const auto mixed = KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrow, 14}, {torch, 14}}, 15);
        RequireDrop(mixed.result.ok && mixed.stacks.size() == 15 && mixed.stacks.front().item.amount == 14 && inventory.Rows().at(torch).amount == 46, "the exact mixed capacity succeeds without splitting arrows");
    }

    void MalformedUnitsAreRefused() {
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"1","amount":1},{"id":"1","amount":1}]})", units), "duplicate row identities must be coalesced by the sender");
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"1","amount":1,"held":1}]})", units), "per-row held markers must be booleans");
        RequireDrop(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"","amount":1}]})", units), "an empty identity is refused");
        RequireDrop(KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"1","amount":1},{"id":"2","amount":1}],"held":true})", units) && !units[0].held && !units[1].held, "legacy multi-row held requests preserve their old meaning");
    }
} // namespace

void DropSelectionTests() {
    OneSelectionSendsOnePayload();
    PartialSelectionPreservesTheRemainderAndNextAction();
    SeveralRowsAreValidatedTogether();
    SelectionBoundsNeverSendAPartialBatch();
    NestedSelectionAndSingleDropsFinishOnce();
    TorchesSplitAfterOnePayloadAndRoundTrip();
    PhysicalStackCapacityIsAtomic();
    MalformedUnitsAreRefused();
}
