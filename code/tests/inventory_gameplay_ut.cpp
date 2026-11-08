#include "features/inventory/inventory_gameplay.h"
#include "features/items/inventory_reconciliation.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace {
    constexpr const char *kApple    = "2264f217-590e-4c0f-a4c6-f50c6532b9f6";
    constexpr const char *kPotion   = "b38c34b7-6016-4f64-9ba2-65e1ce31d4a1";
    constexpr const char *kBandage  = "9fa3000e-3807-48a8-bed8-81427f0bda55";
    constexpr const char *kArrow    = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
    constexpr const char *kSage     = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kSword    = "ce1953df-9f10-4138-8fb1-393a11474553";
    constexpr const char *kBascinet = "701e2d4b-e7bd-406c-b24b-297688ad969e";
    constexpr const char *kBroken   = "3b5be706-3cac-4223-a780-039867c65c4d";
    constexpr const char *kHunting    = "710e3706-8974-404b-b23a-6f51670ef1ed";
    constexpr const char *kBolt       = "40337bef-e965-4a60-abee-695e9a784fa4";
    constexpr const char *kBrokenBolt = "a1bbf4df-3242-498e-9595-7879665c4ee8";

    void RequireGameplay(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    std::string AddRow(KCDC::Features::Inventory::Inventory &inventory, const char *item, std::uint32_t amount) {
        return inventory.Add(item, amount, nlohmann::json::object(), std::nullopt).outputs[0].id;
    }

    void ClassesAndKinds() {
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass("7da54a04-67c4-4767-8b40-ee9211cc465c") && KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass("0d40444a-c79e-4406-b71f-9574a856cef2"),
            "quickslot belts and pouches are persistent equipment");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kApple, KCDC::Features::Inventory::InventoryConsumption::Use), "food is used");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kBandage, KCDC::Features::Inventory::InventoryConsumption::Use), "an ointment is used");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kSage, KCDC::Features::Inventory::InventoryConsumption::Use), "a herb is not eaten");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kArrow, KCDC::Features::Inventory::InventoryConsumption::Use), "an arrow is not used");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kArrow, KCDC::Features::Inventory::InventoryConsumption::Shot), "an arrow is shot");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::IsConsumableClass(kApple, KCDC::Features::Inventory::InventoryConsumption::Shot), "an apple is not shot");
        RequireGameplay(std::string(KCDC::Features::Inventory::InventoryGameplay::UseKind(kApple)) == "food" && std::string(KCDC::Features::Inventory::InventoryGameplay::UseKind(kPotion)) == "potion", "food and potions follow native food types");
        RequireGameplay(std::string(KCDC::Features::Inventory::InventoryGameplay::UseKind("9872a67f-e235-4641-913a-737681f52870")) == "food", "a quest drink named Potion keeps its native drink type");
        RequireGameplay(std::string(KCDC::Features::Inventory::InventoryGameplay::UseKind(kBandage)) == "ointment" && std::string(KCDC::Features::Inventory::InventoryGameplay::UseKind(kArrow)) == "shot", "ointments and rounds have their own kind");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(kSword) && KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(kBascinet) && !KCDC::Features::Inventory::InventoryGameplay::IsEquippableClass(kApple), "only gear is worn");
    }

    void UseSpendsTheNamedUnit() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto apples = AddRow(inventory, kApple, 3);
        const auto sage   = AddRow(inventory, kSage, 2);
        inventory.TakeChanges();

        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{sage, 1}}).code == "notConsumable", "a herb is not eaten");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{apples, 11}}).code == "invalidAmount", "one use spends at most ten");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{"missing", 1}}).ok, "an unknown row spends nothing");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{apples, 4}}).ok && inventory.Rows().at(apples).amount == 3, "no more than is held");

        const auto eaten = KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{apples, 1}});
        RequireGameplay(eaten.ok && inventory.Rows().at(apples).amount == 2, "eating spends exactly the named unit");
        const auto changes = inventory.TakeChanges();
        RequireGameplay(changes.size() == 1 && changes[0].reason == "use", "the spend is recorded as a use");

        // Wear moves the revision all through a fight; the owner's report still lands.
        RequireGameplay(inventory.Remove({{sage, 1}}, std::nullopt).ok, "fixture: something else moves the revision");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Use, {}, {{apples, 1}}).ok && inventory.Rows().at(apples).amount == 1, "a use reported against an older revision still spends exactly the named unit");
    }

    void ShotSpendsARoundAndRemembersIt() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto arrows = AddRow(inventory, kArrow, 5);
        inventory.TakeChanges();

        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, {arrows}, {{arrows, 17}}).code == "invalidAmount", "one shot report spends at most sixteen");
        RequireGameplay(inventory.Remove({{arrows, 1}}, std::nullopt).ok, "fixture: something else moves the revision");
        std::vector<KCDC::Features::Inventory::InventoryRow> spent;
        const auto shot = KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, {arrows}, {{arrows, 1}}, &spent);
        const auto shotChanges = inventory.TakeChanges();
        RequireGameplay(shot.ok && inventory.Rows().at(arrows).amount == 3 && shotChanges.back().reason == "shot", "a shot spends one round, whatever revision it was reported against");
        RequireGameplay(spent.size() == 1 && spent[0].itemClass == kArrow && spent[0].amount == 1, "the spent round keeps its class");

        const auto loose = AddRow(inventory, kHunting, 1);
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, {arrows}, {{loose, 1}}).code == "notLoaded" && inventory.Rows().at(loose).amount == 1, "a round that was never in the quiver cannot be shot");
    }

    void ReadConsumeShape() {
        std::string request;
        KCDC::Features::Inventory::InventoryConsumption kind {};
        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::ReadConsume(R"({"request":"7","kind":"shot","revision":4,"units":[{"id":"2","amount":1}]})"_json, request, kind, units) && request == "7" && kind == KCDC::Features::Inventory::InventoryConsumption::Shot && units.size() == 1, "a consumption names request, kind and units");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadConsume(R"({"request":"7","kind":"eat","revision":4,"units":[{"id":"2","amount":1}]})"_json, request, kind, units), "an unknown kind is refused");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::ReadConsume(R"({"request":"7","kind":"use","units":[{"id":"2","amount":1}]})"_json, request, kind, units), "the spend needs no revision");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadConsume(R"({"request":"7","kind":"use","revision":4,"units":[{"id":"2","amount":1},{"id":"2","amount":1}]})"_json, request, kind, units), "a row named twice is refused");
    }

    void EquipmentIsCountedAndClamped() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sword  = AddRow(inventory, kSword, 2);
        const auto helmet = AddRow(inventory, kBascinet, 1);
        const auto arrows = AddRow(inventory, kArrow, 10);
        const auto spare  = AddRow(inventory, kHunting, 3);
        const auto apples = AddRow(inventory, kApple, 2);

        std::uint64_t revision = 0;
        std::map<std::string, std::uint32_t> reported;
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(nlohmann::json {{"revision", 9U}, {"items", {{sword, 2U}, {arrows, 10U}}}}, revision, reported) && revision == 9 && reported.at(sword) == 2, "an equipment report maps rows to worn units");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(nlohmann::json {{"revision", 9U}, {"items", nlohmann::json::array({sword})}}, revision, reported), "a list of rows is not a report");
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(nlohmann::json {{"revision", 9U}, {"items", {{sword, 0U}}}}, revision, reported), "a row worn zero times is not a report");

        const auto equipped = KCDC::Features::Inventory::InventoryGameplay::CountEquipment(inventory, {{sword, 2}, {helmet, 1}, {arrows, 10}, {spare, 2}, {apples, 1}, {"missing", 1}});
        RequireGameplay(equipped.size() == 3, "unknown rows, food and a second quiver stack are dropped, the rest kept");
        RequireGameplay(equipped.at(sword) == 1 && equipped.at(helmet) == 1, "a piece of gear is worn once");
        RequireGameplay(equipped.at(arrows) == 10 && !equipped.contains(spare), "only the fullest ammunition row is the quiver");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::CountEquipment(inventory, {{arrows, 40}}).at(arrows) == 10, "no more worn than held");

        auto clamped = equipped;
        RequireGameplay(inventory.Remove({{helmet, 1}, {arrows, 9}}, std::nullopt).ok, "fixture drops the helmet and most arrows");
        KCDC::Features::Inventory::InventoryGameplay::ClampEquipment(inventory, clamped);
        RequireGameplay(!clamped.contains(helmet) && clamped.at(arrows) == 1 && clamped.at(sword) == 1, "a worn count follows its row out or down");
    }

    void MergedQuiverRowsRemainEquippedAndSpendable() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const nlohmann::json poisoned {{"poison", "03845da1-2125-45dd-a315-cf7dba569e54"}, {"poisonCharges", 1}};
        const auto restored = inventory.Set({{"101", kArrow, 5}, {"102", kArrow, 4}, {"103", kBolt, 8}, {"104", kArrow, 6}, {"105", kArrow, 8, poisoned}}, std::nullopt);
        RequireGameplay(restored.ok, "fixture: restored ammunition keeps distinct row identities");
        const auto equipped = KCDC::Features::Inventory::InventoryGameplay::CountEquipment(inventory, {{"101", 5}, {"102", 4}, {"103", 8}, {"105", 8}});
        RequireGameplay(equipped == std::map<std::string, std::uint32_t> {{"101", 5}, {"102", 4}, {"103", 8}}, "the fullest native quiver of each ammunition type retains every reported row with matching properties");

        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive(equipped, 1, 1000);
        RequireGameplay(equipment.Poll(1000, {}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "the restored outfit is applied");
        RequireGameplay(equipment.Poll(3000, {{"101", 5}, {"102", 4}, {"103", 8}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete && equipment.CanReport(), "native arrow and bolt quivers spanning several rows verify without retrying the outfit");

        std::set<std::string> loaded;
        for (const auto &[id, amount] : equipped) {
            loaded.insert(id);
        }
        std::vector<KCDC::Features::Inventory::InventoryRow> spent;
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, loaded, {{"102", 1}}, &spent).ok, "the last row in the native stack can be spent");
        RequireGameplay(inventory.Rows().at("101").amount == 5 && inventory.Rows().at("102").amount == 3 && spent.size() == 1 && spent[0].id == "102", "the shot spends exactly one unit of its actual row");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, loaded, {{"103", 1}}).ok && inventory.Rows().at("103").amount == 7, "bolts remain spendable alongside a fuller arrow quiver");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, loaded, {{"104", 1}}).code == "notLoaded", "matching ammunition not reported in the quiver is not authorized");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, loaded, {{"105", 1}}).code == "notLoaded", "poisoned ammunition remains a separate native stack");
    }

    void PocketsExcludeWornUnits() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory victim(ids);
        KCDC::Features::Inventory::Inventory thief(ids);
        const auto sword  = AddRow(victim, kSword, 1);
        const auto apples = AddRow(victim, kApple, 4);
        const auto arrows = AddRow(victim, kArrow, 10);
        const std::map<std::string, std::uint32_t> equipped {{sword, 1}, {arrows, 3}};

        const auto offer = KCDC::Features::Inventory::InventoryGameplay::Pockets(victim, equipped);
        RequireGameplay(offer.size() == 2, "the offer names only what is not worn, one line per class");
        for (const auto &line : offer) {
            RequireGameplay((line.itemClass == kApple && line.amount == 4) || (line.itemClass == kArrow && line.amount == 7), "each line counts the unworn units");
        }
        victim.TakeChanges();

        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Steal(victim, thief, equipped, offer, {{kArrow, 8}}).code == "notOffered", "a claim beyond the offer is refused");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Steal(victim, thief, equipped, offer, {{kSword, 1}}).code == "notOffered", "worn gear cannot be taken");
        const auto stolen = KCDC::Features::Inventory::InventoryGameplay::Steal(victim, thief, equipped, offer, {{kApple, 2}, {kArrow, 7}});
        RequireGameplay(stolen.ok && victim.Rows().at(apples).amount == 2 && victim.Rows().at(arrows).amount == 3 && thief.Rows().size() == 2, "a theft moves exactly the claimed units");
        RequireGameplay(victim.TakeChanges()[0].reason == "pickpocket" && thief.TakeChanges()[0].reason == "pickpocket", "both sides record a pickpocket");

        RequireGameplay(victim.Remove({{apples, 2}}, std::nullopt).ok, "fixture: the victim eats the rest");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Steal(victim, thief, equipped, offer, {{kApple, 1}}).code == "insufficientItems", "a victim who changed meanwhile is refused");
    }

    void DropUnitsKeepWornGear() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sword  = AddRow(inventory, kSword, 1);
        const auto arrows = AddRow(inventory, kArrow, 10);
        const auto apples = AddRow(inventory, kApple, 2);
        const std::map<std::string, std::uint32_t> equipped {{sword, 1}, {arrows, 4}};

        const auto all = KCDC::Features::Inventory::InventoryGameplay::DropUnits(inventory, equipped, false, 128);
        RequireGameplay(all.size() == 3, "without keepEquipped every row goes");
        const auto kept = KCDC::Features::Inventory::InventoryGameplay::DropUnits(inventory, equipped, true, 128);
        RequireGameplay(kept.size() == 2, "the worn sword stays");
        for (const auto &unit : kept) {
            RequireGameplay((unit.id == arrows && unit.amount == 6) || (unit.id == apples && unit.amount == 2), "only the unworn units drop");
        }
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::DropUnits(inventory, equipped, false, 1).size() == 1, "a drop is capped at the container's rows");
    }

    void SpentRoundsComeBackOnce() {
        KCDC::Features::Inventory::SpentRounds rounds;
        const KCDC::Features::Inventory::InventoryRow arrow {"1", kArrow, 1, nlohmann::json::object()};
        const KCDC::Features::Inventory::InventoryRow cutting {"2", "c49aa63a-07a6-4417-9f9b-97f2712a4cd0", 1, nlohmann::json::object()};
        rounds.Fire(arrow, 2, 0, 1000);
        rounds.Fire(cutting, 1, 0, 2000);

        RequireGameplay(!rounds.Claim(kApple, 0, 3000), "only a fired class comes back");
        const auto first = rounds.Claim(kArrow, 0, 3000);
        RequireGameplay(first && first->firedAtMs == 1000 && rounds.Size() == 2, "the oldest round of the class comes back first");
        RequireGameplay(rounds.Claim(kArrow, 0, 3000) && !rounds.Claim(kArrow, 0, 3000), "a round comes back only once");
        RequireGameplay(!rounds.Claim(cutting.itemClass, 0, 2000 + KCDC::Features::Inventory::SpentRounds::kLifetimeMs), "a round is gone after twenty minutes");

        for (std::uint32_t i = 0; i < KCDC::Features::Inventory::SpentRounds::kMaximum + 5; ++i) {
            rounds.Fire(arrow, 1, 0, 10000 + i);
        }
        RequireGameplay(rounds.Size() == KCDC::Features::Inventory::SpentRounds::kMaximum, "at most 128 rounds are remembered");
        RequireGameplay(rounds.Claim(kArrow, 0, 20000)->firedAtMs == 10005, "the oldest are forgotten first");

        KCDC::Features::Inventory::SpentRounds elsewhere;
        elsewhere.Fire(arrow, 1, 7, 1000);
        RequireGameplay(!elsewhere.Claim(kArrow, 0, 2000) && elsewhere.Claim(kArrow, 7, 2000), "a round comes back only in the world it was fired in");

        RequireGameplay(KCDC::Features::Inventory::SpentRounds::WithinReach({0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F}) && !KCDC::Features::Inventory::SpentRounds::WithinReach({0.0F, 0.0F, 0.0F}, {301.0F, 0.0F, 0.0F}), "a round lands within a bow's reach of its archer");
    }
} // namespace

namespace {
    void BrokenRoundsLandBroken() {
        RequireGameplay(KCDC::Features::Inventory::SpentRounds::LandsAs(kArrow, kArrow), "a whole round lands as itself");
        RequireGameplay(KCDC::Features::Inventory::SpentRounds::LandsAs(kArrow, kBroken), "a round that broke on impact lands as a broken round");
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::LandsAs(kArrow, kSword), "a round never lands as anything else");
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::LandsAs(kArrow, kBrokenBolt), "an arrow never breaks into a bolt");
        RequireGameplay(KCDC::Features::Inventory::SpentRounds::LandsAs(kBolt, kBrokenBolt) && !KCDC::Features::Inventory::SpentRounds::LandsAs(kBolt, kBroken), "a bolt breaks into a broken bolt");
    }
} // namespace

namespace {
    void RecoveryKeepsTheImpactIdentityAndPose() {
        KCDC::Features::Inventory::AmmunitionRecovery recovery;
        for (const auto *ammunition : {kArrow, kBolt, kBroken, kBrokenBolt}) {
            const auto *fired = ammunition == kBroken ? kArrow : ammunition == kBrokenBolt ? kBolt : ammunition;
            const nlohmann::json message {{"item", fired}, {"landed", ammunition}, {"position", {12.5F, 30.0F, 4.25F}}, {"rotation", {0.5F, 0.5F, 0.5F, 0.5F}}, {"nativeItemWuid", 0xF123456789ABCDEFULL}};
            RequireGameplay(KCDC::Features::Inventory::SpentRounds::ReadRecovery(nlohmann::json::parse(message.dump()), recovery), "whole and broken arrows and bolts retain their impact pose");
            RequireGameplay(recovery.nativeItemWuid == 0xF123456789ABCDEFULL && recovery.position == glm::vec3(12.5F, 30.0F, 4.25F) && recovery.rotation == glm::quat(0.5F, 0.5F, 0.5F, 0.5F), "the full native identity and orientation survive the recovery message");
        }
        nlohmann::json message {{"item", kArrow}, {"landed", kArrow}, {"position", {1.0F, 2.0F, 3.0F}}};
        RequireGameplay(KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery) && recovery.nativeItemWuid == 0 && recovery.rotation == glm::quat(1.0F, 0.0F, 0.0F, 0.0F), "body retention without a native world pickable keeps its existing recovery path");
        message["nativeItemWuid"] = std::uint64_t(42);
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "adoption without the impact rotation is refused");
        message["rotation"] = {0.0F, 0.0F, 0.0F, 0.0F};
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "a zero rotation cannot reach a native transform");
        message["rotation"] = {1.0F, 0.0F, 0.0F, 0.0F};
        for (const nlohmann::json &invalid : {nlohmann::json(-1), nlohmann::json(42.5), nlohmann::json("42"), nlohmann::json(std::uint64_t(0))}) {
            message["nativeItemWuid"] = invalid;
            RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "only a nonzero unsigned native identity can be adopted");
        }
        message["nativeItemWuid"] = std::uint64_t(42);
        message["position"][0]    = std::numeric_limits<double>::max();
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "coordinates that overflow native floats are refused");
        message["position"][0] = 1.0F;
        message["rotation"][0] = std::numeric_limits<float>::infinity();
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "nonfinite impact rotations are refused");
        message["rotation"] = {1.0F, 0.0F, 0.0F};
        RequireGameplay(!KCDC::Features::Inventory::SpentRounds::ReadRecovery(message, recovery), "truncated impact rotations are refused");
    }

    void RacksAndSmokehousesMakeTheNativeProducts() {
        constexpr const char *kDriedSage  = "2bb1c148-8ee1-42bc-8a93-8c456a57eba5";
        constexpr const char *kBeef       = "81c21fdc-3d62-4d1f-854f-eb364db1bcff";
        constexpr const char *kDriedBeef  = "b1de7a91-1644-4ad6-b186-a5b40764be7f";
        constexpr const char *kSmokedBeef = "208362ca-1006-4821-8118-227e64143a3e";
        constexpr const char *kCookedBeef = "44593169-7482-4293-80bc-01c3144e4fa2";
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const auto sage = inventory.Add(kSage, 3, {{"health", 0.375}}, std::nullopt);
        const auto beef = inventory.Add(kBeef, 2, {{"health", 0.5}}, std::nullopt);
        RequireGameplay(sage.ok && beef.ok, "fixture: a wilted herb and half-fresh beef");
        inventory.TakeChanges();

        std::string request;
        KCDC::Shared::Game::PreparationKind kind {};
        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        const nlohmann::json smoke {{"request", "7"}, {"kind", "smoke"}, {"units", {{{"id", beef.outputs[0].id}, {"amount", 2U}}}}};
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::ReadPreparation(smoke, request, kind, units) && request == "7" && kind == KCDC::Shared::Game::PreparationKind::Smoking && units.size() == 1 && units[0].amount == 2, "a smoking report is read whole");
        nlohmann::json cook = smoke;
        cook["kind"]        = "cook";
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::ReadPreparation(cook, request, kind, units) && kind == KCDC::Shared::Game::PreparationKind::Cooking, "cooking at a fire is read as one");
        cook["kind"] = "boil";
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadPreparation(cook, request, kind, units), "nothing else is");
        nlohmann::json twice      = smoke;
        const nlohmann::json unit = twice["units"][0];
        twice["units"].push_back(unit);
        RequireGameplay(!KCDC::Features::Inventory::InventoryGameplay::ReadPreparation(twice, request, kind, units), "a row is named once");

        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Smoking, {{sage.outputs[0].id, 3}}).empty(), "a smokehouse makes nothing of a herb");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Drying, {{"missing", 1}}).empty(), "nor of a row the player does not have");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Smoking, {{beef.outputs[0].id, 2}}).at(0).itemClass == kSmokedBeef, "a smokehouse smokes beef");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Cooking, {{sage.outputs[0].id, 3}}).empty(), "a fire cooks no herb");
        const auto cooked = KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Cooking, {{beef.outputs[0].id, 2}});
        RequireGameplay(cooked.size() == 1 && cooked[0].itemClass == kCookedBeef && cooked[0].metadata == inventory.Rows().at(beef.outputs[0].id).metadata, "a fire cooks beef, which keeps the health it had");
        const std::vector<KCDC::Features::Inventory::InventoryUnit> hung {{sage.outputs[0].id, 3}, {beef.outputs[0].id, 1}};
        const auto dried = KCDC::Features::Inventory::InventoryGameplay::Prepared(inventory, KCDC::Shared::Game::PreparationKind::Drying, hung);
        RequireGameplay(dried.size() == 2 && dried[0].itemClass == kDriedSage && dried[0].amount == 3 && dried[0].metadata.empty(), "a herb dries into its dried kind at full health");
        RequireGameplay(dried[1].itemClass == kDriedBeef && dried[1].amount == 1 && dried[1].metadata == inventory.Rows().at(beef.outputs[0].id).metadata, "dried food keeps the health it had");
        RequireGameplay(inventory.Exchange(hung, dried, inventory.Revision(), "dry").ok && !inventory.Rows().contains(sage.outputs[0].id) && inventory.Rows().at(beef.outputs[0].id).amount == 1, "the conversion takes the rows and gives the products together");
        const auto changes = inventory.TakeChanges();
        RequireGameplay(changes.size() == 1 && changes[0].reason == "dry", "it is one dry change");
    }

    void ATheftTakesTheChargeAndReach() {
        const glm::vec3 thief(0.0F, 0.0F, 0.0F);
        const glm::vec3 near(1.0F, 0.0F, 0.0F);
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::TheftRefusal(1000, 1000 + KCDC::Features::Inventory::InventoryGameplay::kMinimumTheftMs, thief, 0, near, 0).empty(), "a theft after the charge could fill, within reach, stands");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::TheftRefusal(1000, 1500, thief, 0, near, 0) == "tooEarly", "a theft the native charge could not have reached is refused");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::TheftRefusal(1000, 9000, thief, 0, {40.0F, 0.0F, 0.0F}, 0) == "tooFar", "a thief who walked away takes nothing");
        RequireGameplay(KCDC::Features::Inventory::InventoryGameplay::TheftRefusal(1000, 9000, thief, 0, near, 3) == "otherWorld", "nor one in another world");
    }
} // namespace

void InventoryGameplayTests() {
    ClassesAndKinds();
    UseSpendsTheNamedUnit();
    ShotSpendsARoundAndRemembersIt();
    ReadConsumeShape();
    EquipmentIsCountedAndClamped();
    MergedQuiverRowsRemainEquippedAndSpendable();
    PocketsExcludeWornUnits();
    DropUnitsKeepWornGear();
    SpentRoundsComeBackOnce();
    BrokenRoundsLandBroken();
    RecoveryKeepsTheImpactIdentityAndPose();
    ATheftTakesTheChargeAndReach();
    RacksAndSmokehousesMakeTheNativeProducts();
}
