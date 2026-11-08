#include "features/ground_item/ground_stock.h"
#include "features/inventory/inventory_gameplay.h"
#include "shared/game/item_classes.h"
#include "shared/inventory_item_properties.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {
    constexpr const char *kSage            = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kArrow           = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
    constexpr const char *kTournamentArrow = "c20b2a42-8d5c-48bc-ad24-a4a529207ca9";
    constexpr const char *kTournamentBolt  = "a431da4e-3472-4bc8-9817-f5357ebf853b";
    constexpr const char *kBolt            = "8460003f-637f-4713-92c9-4954037c4b9c";

    void RequireGround(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void DropTakesTheNamedUnits() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage = inventory.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        inventory.TakeChanges();

        RequireGround(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{"missing", 1}}).result.code == "unknownItem", "an unknown row drops nothing");
        RequireGround(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 6}}).result.code == "insufficientItems", "a drop cannot take more than is held");

        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 2}});
        RequireGround(dropped.result.ok && inventory.Rows().at(sage).amount == 3, "a drop removes exactly the named units");
        RequireGround(dropped.stacks.size() == 1 && dropped.stacks[0].item.amount == 2 && dropped.stacks[0].item.itemClass == KCDC::Shared::Game::ItemClassIdFromGuid(kSage) && dropped.stacks[0].item.quality == inventory.Rows().at(sage).metadata.value("quality", 0U),
            "the stack carries the row's class, amount and properties");
        const auto changes = inventory.TakeChanges();
        RequireGround(changes.size() == 1 && changes[0].reason == "drop", "the removal is recorded as a drop");
    }

    void EachStackOfARowDropsOnItsOwn() {
        // Separate actions or old clients can still send consecutive drops
        // before the preceding inventory revision is acknowledged.
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage = inventory.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto before = inventory.Revision();
        RequireGround(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 2}}).result.ok && inventory.Revision() != before, "the first stack of the row drops");
        RequireGround(KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{sage, 3}}).result.ok && inventory.Rows().empty(), "the next stack drops though the first moved the revision");
    }

    void PickupAddsTheStack() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory source(ids), target(ids);
        const auto sage = source.Add(kSage, 4, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto held = target.Add(kSage, 1, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(source, {{sage, 4}});
        RequireGround(dropped.result.ok && source.Rows().empty(), "a whole row can be dropped");
        target.TakeChanges();

        const auto picked = KCDC::Features::GroundItem::GroundStock::Pickup(target, dropped.stacks[0].item);
        RequireGround(picked.ok && target.Rows().size() == 1 && target.Rows().at(held).amount == 5, "a picked up stack merges into the equal row");
        const auto changes = target.TakeChanges();
        RequireGround(changes.size() == 1 && changes[0].reason == "pickup", "the grant is recorded as a pickup");
    }

    void PickupIntoAFullInventoryIsRefused() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory source(ids), full(ids, 1);
        const auto arrows = source.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireGround(full.Add(kSage, 1, nlohmann::json::object(), std::nullopt).ok, "fixture fills the only row");
        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(source, {{arrows, 3}});
        RequireGround(KCDC::Features::GroundItem::GroundStock::Pickup(full, dropped.stacks[0].item).code == "inventoryCapacity" && full.Rows().size() == 1, "a full inventory refuses the stack and keeps its rows");
    }

    void TournamentAmmunitionCanBeRecoveredAndPickedUp() {
        for (const auto *ammunition : {kTournamentArrow, kArrow, kTournamentBolt, kBolt}) {
            KCDC::Features::Inventory::InventoryIds ids;
            KCDC::Features::Inventory::Inventory shooter(ids), collector(ids);
            const auto row = shooter.Add(ammunition, 1, {{"quality", 1}}, std::nullopt).outputs[0].id;
            std::vector<KCDC::Features::Inventory::InventoryRow> spent;
            RequireGround(KCDC::Features::Inventory::InventoryGameplay::Consume(shooter, KCDC::Features::Inventory::InventoryConsumption::Shot, {row}, {{row, 1}}, &spent).ok && shooter.Rows().empty() && spent.size() == 1,
                "tournament and ordinary ammunition each spend one loaded round");
            KCDC::Features::Inventory::SpentRounds rounds;
            rounds.Fire(spent[0], 1, 0, 1000);

            KCDC::Features::Inventory::AmmunitionRecovery recovery;
            const nlohmann::json message {{"item", ammunition}, {"landed", ammunition}, {"position", {2427.929F, 2538.21F, 170.3461F}}, {"rotation", {0.9723699F, 0.0F, 0.0F, 0.2334454F}}, {"nativeItemWuid", std::uint64_t(0xF123456789ABCDEFULL)}};
            RequireGround(KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery) && recovery.nativeItemWuid == 0xF123456789ABCDEFULL, "a tournament target impact keeps its native identity");
            const auto recovered = rounds.Claim(recovery.fired, 0, 2000);
            RequireGround(recovered.has_value() && !rounds.Claim(recovery.fired, 0, 2000), "one spent round authorizes exactly one recovered ground item");

            KCDC::Shared::RPC::InventoryItem stack;
            stack.inventoryItemId = 1;
            stack.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid(recovery.landed);
            stack.amount          = 1;
            RequireGround(KCDC::Shared::Inventory::ApplyItemProperties(recovered->metadata, stack), "the recovered round retains its inventory properties");
            KCDC::Features::GroundItem::GroundStock::ApplyClassDefaults(stack);
            RequireGround(KCDC::Features::GroundItem::GroundStock::CanPlace(stack), "tournament ammunition is a valid recoverable ground item");
            RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(recovery.position, 0, true, recovery.position, 0, true).empty(), "a character at the target may pick up the resting round");
            RequireGround(KCDC::Features::GroundItem::GroundStock::Pickup(collector, stack).ok && collector.Rows().size() == 1, "another character can collect the recovered round");
            const auto &picked = collector.Rows().begin()->second;
            RequireGround(picked.itemClass == ammunition && picked.amount == 1, "pickup returns exactly one round of the fired class");
        }
    }

    void OnlyAWholeHeldRowLeavesTheHand() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto arrows = inventory.Add(kArrow, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireGround(!KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrows, 2, true}}).stacks[0].retiresHeldItem, "part of a held stack leaves the rest in hand");
        RequireGround(!KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrows, 1}}).stacks[0].retiresHeldItem, "a stack not in hand retires nothing");
        const auto whole = KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrows, 2, true}});
        RequireGround(whole.result.ok && whole.stacks[0].retiresHeldItem, "the whole held stack leaves the hand with the drop");
    }

    void PickupNeedsReach() {
        const glm::vec3 stack(10.0F, 10.0F, 0.0F);
        RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(glm::vec3(11.0F, 10.0F, 0.0F), 0, true, stack, 0, true).empty(), "a player beside the stack may take it");
        RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(glm::vec3(20.0F, 10.0F, 0.0F), 0, true, stack, 0, true) == "tooFar", "a distant player may not");
        RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(glm::vec3(11.0F, 10.0F, 0.0F), 3, true, stack, 0, true) == "otherWorld", "nor one in another virtual world");
        RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(glm::vec3(11.0F, 10.0F, 0.0F), 0, false, stack, 0, true) == "cannotAct", "nor one who cannot act");
        RequireGround(KCDC::Features::GroundItem::GroundStock::PickupRefusal(glm::vec3(11.0F, 10.0F, 0.0F), 0, true, stack, 0, false) == "falling", "a stack still falling has no pose the server vouches for");
    }

    void ADropOnlyTakesWhatCanBePlaced() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto arrows = inventory.Add(kArrow, 4, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto dropped = KCDC::Features::GroundItem::GroundStock::Drop(inventory, {{arrows, 4}});
        RequireGround(dropped.result.ok && dropped.stacks.size() == 1 && KCDC::Features::GroundItem::GroundStock::CanPlace(dropped.stacks[0].item), "a dropped stack is one every receiver can build");
        RequireGround(dropped.stacks[0].item.quality >= 1 && dropped.stacks[0].item.health >= 0.0F, "a missile stack is completed with the class defaults");

        KCDC::Shared::RPC::InventoryItem broken = dropped.stacks[0].item;
        broken.amount                           = 0;
        RequireGround(!KCDC::Features::GroundItem::GroundStock::CanPlace(broken), "an empty stack cannot be placed");
    }

    void DropRequestsAreBounded() {
        std::string body = R"({"units":[)";
        for (std::size_t i = 0; i <= KCDC::Features::GroundItem::GroundStock::kMaximumDropUnits; ++i) {
            body += (i ? "," : "") + std::string(R"({"id":")") + std::to_string(i + 1) + R"(","amount":1})";
        }
        body += "]}";
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireGround(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(body, units), "a drop naming more rows than one request may is refused");
    }

    void StacksPerPlayerAreBounded() {
        KCDC::Features::GroundItem::GroundStockCounts counts;
        for (std::uint64_t stack = 1; stack <= KCDC::Features::GroundItem::GroundStockCounts::kMaximumPerPlayer; ++stack) {
            counts.Add(stack, 7);
        }
        RequireGround(!counts.CanAdd(7, 1) && counts.CanAdd(8, 1), "one player's live stacks are bounded, another's are not");
        counts.Remove(3);
        RequireGround(counts.CanAdd(7, 1) && !counts.CanAdd(7, 2), "a stack leaving the ground frees its place");
        counts.Remove(3);
        RequireGround(counts.Count(7) == KCDC::Features::GroundItem::GroundStockCounts::kMaximumPerPlayer - 1, "forgetting a stack twice counts it once");
    }

    void AFallEndsNearWhereItStarted() {
        const glm::vec3 origin(100.0F, 100.0F, 20.0F);
        RequireGround(KCDC::Features::GroundItem::GroundStock::SettledPosition(origin, glm::vec3(101.0F, 100.5F, 19.2F)) == glm::vec3(101.0F, 100.5F, 19.2F), "a stack that fell near where it was dropped rests where it fell");
        RequireGround(KCDC::Features::GroundItem::GroundStock::SettledPosition(origin, glm::vec3(160.0F, 100.0F, 20.0F)) == origin, "a report of a stack flying off stays where it was dropped");
        RequireGround(KCDC::Features::GroundItem::GroundStock::SettledPosition(origin, glm::vec3(std::nanf(""), 0.0F, 0.0F)) == origin, "a pose that is not a pose is refused");
    }

    void DropRequestsAreRead() {
        std::vector<KCDC::Shared::Inventory::DropUnit> units;
        RequireGround(KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"12","amount":3}]})", units) && units.size() == 1 && units[0].id == "12" && units[0].amount == 3 && !units[0].held, "a drop request names its units");
        RequireGround(KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"12","amount":3}],"held":true})", units) && units[0].held, "a drop request may say the stack was in hand");
        RequireGround(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"12","amount":3}],"held":1})", units), "held must be a boolean");
        RequireGround(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[{"id":"12","amount":0}]})", units), "a zero amount is refused");
        RequireGround(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest(R"({"units":[]})", units), "an empty drop is refused");
        RequireGround(!KCDC::Features::GroundItem::GroundStock::ReadDropRequest("not json", units), "a malformed body is refused");
    }
} // namespace

void GroundStockTests() {
    TournamentAmmunitionCanBeRecoveredAndPickedUp();
    DropTakesTheNamedUnits();
    EachStackOfARowDropsOnItsOwn();
    PickupAddsTheStack();
    PickupIntoAFullInventoryIsRefused();
    OnlyAWholeHeldRowLeavesTheHand();
    PickupNeedsReach();
    DropRequestsAreRead();
    ADropOnlyTakesWhatCanBePlaced();
    DropRequestsAreBounded();
    StacksPerPlayerAreBounded();
    AFallEndsNearWhereItStarted();
}
