#pragma once

#include "../server/src/features/inventory/inventory_gameplay.h"
#include "features/items/native_repair_batch.h"
#include "shared/inventory_property_policy.h"

#include <stdexcept>

namespace InventoryRepairTests {
    constexpr const char *kKit   = "c707733a-c0a7-4f02-b684-9392b0b15b83";
    constexpr const char *kSword = "ce1953df-9f10-4138-8fb1-393a11474553";

    struct Fixture {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory {ids};
        KCDC::Features::Items::NativeRepairBatch batch;
        std::map<std::uint64_t, KCDC::Features::Items::NativeRepairPart> native;
        std::string kit;
        std::string sword;
        std::uint64_t pendingKit       = 0;
        std::uint64_t pendingEquipment = 0;

        explicit Fixture(std::uint32_t kits = 1, std::uint32_t swords = 3) {
            kit   = inventory.Add(kKit, kits, {{"health", 0.8}}, std::nullopt).outputs.at(0).id;
            sword = inventory.Add(kSword, swords, {{"quality", 2}, {"condition", 0.3}}, std::nullopt).outputs.at(0).id;
            inventory.TakeChanges();
        }

        void Bind(std::uint64_t id, const std::string &row) {
            const auto &metadata = inventory.Rows().at(row).metadata;
            native[id]           = {id, row, metadata, metadata};
        }

        void Observe(std::uint64_t kitId, std::uint64_t equipmentId, bool equipped = false, bool canSpend = true) {
            Finish();
            pendingKit       = kitId;
            pendingEquipment = equipmentId;
            batch.Observe(kitId, equipmentId, equipped, canSpend, [&](std::uint64_t id, const auto &taken, auto &part) {
                const auto &item = native.at(id);
                const auto &row  = inventory.Rows().at(item.rowId);
                const auto count = std::count_if(taken.begin(), taken.end(), [&](const auto &other) {
                    return other.rowId == item.rowId;
                });
                // Like projection, a new claim must still match the original
                // row. Reused kits already differ, so must not be read again.
                if (item.after != row.metadata || count >= row.amount) {
                    return false;
                }
                part = item;
                return true;
            });
        }

        void Change(std::uint64_t id, const nlohmann::json &properties, bool spent = false) {
            auto &item = native.at(id);
            std::string error;
            if (!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(inventory.Rows().at(item.rowId).itemClass, properties, item.after, error)) {
                throw std::runtime_error(error);
            }
            item.spent = spent;
        }

        void Finish() {
            if (pendingKit == 0) {
                return;
            }
            batch.CaptureResult(pendingKit, pendingEquipment, [&](bool kitPart, auto &part) {
                const auto &item = native.at(part.nativeId);
                if (!kitPart && item.spent) {
                    return false;
                }
                part.after = item.after;
                part.spent = item.spent;
                return true;
            });
            pendingKit = pendingEquipment = 0;
        }

        bool Complete() {
            Finish();
            return batch.Complete();
        }

        KCDC::Features::Inventory::InventoryResult Submit() {
            auto message       = batch.Encode();
            message["request"] = "1";
            std::string request;
            std::vector<KCDC::Features::Inventory::InventoryRepairPart> kits;
            std::vector<KCDC::Features::Inventory::InventoryRepairPart> equipment;
            if (!KCDC::Features::Inventory::InventoryGameplay::ReadRepair(message, request, kits, equipment)) {
                throw std::runtime_error("repair message rejected");
            }
            return inventory.Repair(kits, equipment);
        }

        std::uint32_t Amount(const char *itemClass, const nlohmann::json &properties) const {
            std::uint32_t amount = 0;
            std::string error;
            nlohmann::json metadata;
            if (!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(itemClass, properties, metadata, error)) {
                throw std::runtime_error(error);
            }
            for (const auto &[id, row] : inventory.Rows()) {
                if (row.itemClass == itemClass && row.metadata == metadata) {
                    amount += row.amount;
                }
            }
            return amount;
        }
    };
} // namespace InventoryRepairTests

MODULE(inventory_repairs, {
    IT("keeps results when repaired equipment merges before the next update", {
        InventoryRepairTests::Fixture fixture(1, 2);
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.sword);
        fixture.Bind(3, fixture.sword);
        fixture.Observe(1, 2);
        fixture.Change(1, {{"health", 0.6}});
        fixture.Change(2, {{"quality", 2}, {"condition", 1.0}});
        fixture.Finish();
        fixture.native.erase(2); // CombineItems merges the repaired copy.
        fixture.Observe(1, 3);
        fixture.Change(1, {{"health", 0.4}});
        fixture.Change(3, {{"quality", 2}, {"condition", 1.0}});
        fixture.Finish();
        fixture.native.erase(3);
        EQUALS(fixture.Complete(), true);
        EQUALS(fixture.Submit().ok, true);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 1.0}}), 2U);
    });

    IT("does not consume a surviving kit whose native ID disappears in a merge", {
        InventoryRepairTests::Fixture fixture(1, 1);
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.sword);
        fixture.Observe(1, 2);
        fixture.Change(1, {{"health", 0.4}});
        fixture.Change(2, {{"quality", 2}, {"condition", 1.0}});
        fixture.Finish();
        fixture.native.erase(1);
        EQUALS(fixture.Complete(), true);
        EQUALS(fixture.batch.Encode().at("kits").at(0).at("spent").get<bool>(), false);
        EQUALS(fixture.Submit().ok, true);
        EQUALS(fixture.Amount(InventoryRepairTests::kKit, {{"health", 0.4}}), 1U);
    });

    IT("refuses a batch with a missing post-repair result", {
        InventoryRepairTests::Fixture fixture(1, 1);
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.sword);
        fixture.Observe(1, 2);
        EQUALS(fixture.batch.Complete(), false);
    });

    IT("repairs multiple selected items with one reused kit in one revision", {
        InventoryRepairTests::Fixture fixture;
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.sword);
        fixture.Bind(3, fixture.sword);
        fixture.Observe(1, 2);
        fixture.Change(1, {{"health", 0.6}});
        fixture.Change(2, {{"quality", 2}, {"condition", 0.8}});
        fixture.Observe(1, 3);
        fixture.Change(1, {{"health", 0.4}});
        fixture.Change(3, {{"quality", 2}, {"condition", 1.0}});
        EQUALS(fixture.batch.Refused(), false);
        EQUALS(fixture.Complete(), true);
        EQUALS(fixture.batch.Encode().at("kits").size(), 1U);
        EQUALS(fixture.batch.Encode().at("equipment").size(), 2U);
        const auto revision = fixture.inventory.Revision();
        EQUALS(fixture.Submit().ok, true);
        EQUALS(fixture.inventory.Revision(), revision + 1);
        EQUALS(fixture.inventory.TakeChanges().size(), 1U);
        EQUALS(fixture.Amount(InventoryRepairTests::kKit, {{"health", 0.4}}), 1U);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 0.3}}), 1U);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 0.8}}), 1U);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 1.0}}), 1U);
    });

    IT("spends exhausted kits once across items and preserves the worn row in either order", {
        for (const bool wornFirst : {false, true}) {
            InventoryRepairTests::Fixture fixture(3, 3);
            fixture.Bind(1, fixture.kit);
            fixture.Bind(2, fixture.kit);
            fixture.Bind(3, fixture.sword);
            fixture.Bind(4, fixture.sword);
            fixture.Observe(1, 3, wornFirst);
            fixture.Change(1, {{"health", 0.3}});
            fixture.Change(3, {{"quality", 2}, {"condition", 0.7}});
            fixture.Observe(1, 4, !wornFirst);
            fixture.Change(1, {{"health", 0.0}}, true);
            fixture.Change(4, {{"quality", 2}, {"condition", 0.5}});
            fixture.Observe(2, 4, !wornFirst);
            fixture.Change(2, {{"health", 0.6}});
            fixture.Change(4, {{"quality", 2}, {"condition", 1.0}});
            EQUALS(fixture.Complete(), true);
            EQUALS(fixture.Submit().ok, true);
            EQUALS(fixture.inventory.Rows().at(fixture.sword).amount, 1U);
            EQUALS(fixture.inventory.Rows().at(fixture.sword).metadata == fixture.native.at(wornFirst ? 3 : 4).after, true);
            EQUALS(fixture.Amount(InventoryRepairTests::kKit, {{"health", 0.8}}), 1U);
            EQUALS(fixture.Amount(InventoryRepairTests::kKit, {{"health", 0.6}}), 1U);
            EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 0.3}}), 1U);
        }
    });

    IT("keeps single-item repairs working when one combine needs several kits", {
        InventoryRepairTests::Fixture fixture(2, 1);
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.kit);
        fixture.Bind(3, fixture.sword);
        fixture.Observe(1, 3, true);
        fixture.Change(1, {{"health", 0.0}}, true);
        fixture.Change(3, {{"quality", 2}, {"condition", 0.6}});
        fixture.Observe(2, 3, true);
        fixture.Change(2, {{"health", 0.5}});
        fixture.Change(3, {{"quality", 2}, {"condition", 1.0}});
        EQUALS(fixture.Complete(), true);
        EQUALS(fixture.batch.Encode().at("equipment").size(), 1U);
        EQUALS(fixture.Submit().ok, true);
        EQUALS(fixture.inventory.Rows().at(fixture.sword).metadata == fixture.native.at(3).after, true);
        EQUALS(fixture.Amount(InventoryRepairTests::kKit, {{"health", 0.5}}), 1U);
    });

    IT("tracks all touched units after refusal and resets cleanly for the next selection", {
        InventoryRepairTests::Fixture fixture(1, 1);
        fixture.Bind(1, fixture.kit);
        fixture.Bind(2, fixture.sword);
        fixture.Bind(3, fixture.sword);
        fixture.Observe(1, 2);
        fixture.Observe(1, 3); // A second unit cannot be claimed from one copy.
        EQUALS(fixture.batch.Refused(), true);
        fixture.batch.Observe(4, 5, false, false, [](auto, const auto &, auto &) {
            return false;
        });
        EQUALS(fixture.batch.Touched().size(), 5U);
        EQUALS(fixture.Complete(), false);
        fixture.batch = {};
        EQUALS(fixture.batch.Empty(), true);
        fixture.Observe(1, 2);
        EQUALS(fixture.batch.Refused(), false);
    });

    IT("refuses the entire selection for a stale or invalid second item", {
        for (const bool stale : {false, true}) {
            InventoryRepairTests::Fixture fixture;
            const auto &kit   = fixture.inventory.Rows().at(fixture.kit);
            const auto &sword = fixture.inventory.Rows().at(fixture.sword);
            const KCDC::Features::Inventory::InventoryRepairPart cost {kit.id, kit.metadata, {{"health", 0.4}}};
            const KCDC::Features::Inventory::InventoryRepairPart first {sword.id, sword.metadata, {{"quality", 2}, {"condition", 0.8}}};
            auto second = first;
            if (stale) {
                second.before["quality"] = 1;
            }
            else {
                second.after = second.before;
            }
            const auto rows     = fixture.inventory.Rows();
            const auto revision = fixture.inventory.Revision();
            EQUALS(fixture.inventory.Repair({cost}, {first, second}).ok, false);
            EQUALS(fixture.inventory.Rows() == rows, true);
            EQUALS(fixture.inventory.Revision(), revision);
            EQUALS(fixture.inventory.TakeChanges().empty(), true);
        }
    });

    IT("checks capacity after all selected items and refuses without charging a kit", {
        InventoryRepairTests::Fixture fixture;
        KCDC::Features::Inventory::Inventory limited(fixture.ids, 2);
        const auto kit   = limited.Add(InventoryRepairTests::kKit, 1, {{"health", 0.8}}, std::nullopt).outputs.at(0).id;
        const auto sword = limited.Add(InventoryRepairTests::kSword, 3, {{"quality", 2}, {"condition", 0.3}}, std::nullopt).outputs.at(0).id;
        limited.TakeChanges();
        const auto rows = limited.Rows();
        const KCDC::Features::Inventory::InventoryRepairPart cost {kit, rows.at(kit).metadata, {{"health", 0.4}}};
        const KCDC::Features::Inventory::InventoryRepairPart item {sword, rows.at(sword).metadata, {{"quality", 2}, {"condition", 1.0}}};
        EQUALS(limited.Repair({cost}, {item, item}).code, std::string("inventoryCapacity"));
        EQUALS(limited.Rows() == rows, true);
        EQUALS(limited.TakeChanges().empty(), true);
    });

    IT("cannot spend outputs as extra starting kits or equipment", {
        InventoryRepairTests::Fixture fixture(1, 1);
        const auto rows = fixture.inventory.Rows();
        const KCDC::Features::Inventory::InventoryRepairPart cost {fixture.kit, rows.at(fixture.kit).metadata, {{"health", 0.4}}};
        const KCDC::Features::Inventory::InventoryRepairPart item {fixture.sword, rows.at(fixture.sword).metadata, {{"quality", 2}, {"condition", 1.0}}};
        EQUALS(fixture.inventory.Repair({cost, cost}, {item}).ok, false);
        EQUALS(fixture.inventory.Repair({cost}, {item, item}).ok, false);
        EQUALS(fixture.inventory.Rows() == rows, true);
        EQUALS(fixture.inventory.TakeChanges().empty(), true);
    });

    IT("accepts legacy single repairs and bounds and validates batch messages", {
        const nlohmann::json part {{"id", "1"}, {"before", {{"health", 0.5}}}, {"after", {{"health", 0.8}}}};
        auto kit     = part;
        kit["spent"] = false;
        nlohmann::json message {{"request", "test"}, {"equipment", part}, {"equipped", true}, {"kits", {kit}}};
        std::string request;
        std::vector<KCDC::Features::Inventory::InventoryRepairPart> kits;
        std::vector<KCDC::Features::Inventory::InventoryRepairPart> equipment;
        const auto read = [&]() {
            return KCDC::Features::Inventory::InventoryGameplay::ReadRepair(message, request, kits, equipment);
        };
        EQUALS(read(), true);
        EQUALS(equipment.size(), 1U);
        EQUALS(equipment.at(0).equipped, true);
        auto selected        = part;
        selected["equipped"] = false;
        message["equipment"] = nlohmann::json::array({selected, selected});
        EQUALS(read(), true);
        EQUALS(equipment.size(), 2U);
        message["equipment"][1]["equipped"] = "yes";
        EQUALS(read(), false);
        EQUALS(equipment.size(), 2U); // Failure does not expose partial parsing.
        message["equipment"] = nlohmann::json::array();
        EQUALS(read(), false);
        for (std::size_t index = 0; index <= KCDC::Shared::Inventory::kMaximumRepairEquipment; ++index) {
            message["equipment"].push_back(selected);
        }
        EQUALS(read(), false);
        message["equipment"].erase(message["equipment"].end() - 1);
        EQUALS(read(), true);
        message["kits"] = nlohmann::json::array();
        EQUALS(read(), false);
        for (std::size_t index = 0; index <= KCDC::Shared::Inventory::kMaximumRepairKits; ++index) {
            message["kits"].push_back(kit);
        }
        EQUALS(read(), false);
    });

    IT("sharpens only what a grindstone takes, and only its health", {
        InventoryRepairTests::Fixture fixture(1, 3);
        const auto before  = fixture.inventory.Rows().at(fixture.sword).metadata;
        const auto sharpen = [&](const std::string &id, const nlohmann::json &after) {
            return fixture.inventory.Sharpen({id, fixture.inventory.Rows().at(id).metadata, after});
        };
        EQUALS(sharpen(fixture.kit, {{"health", 1.0}}).code, std::string("invalidSharpen"));
        // A stub (sub class 6) is a melee weapon the grindstone does not take.
        const auto stub = fixture.inventory.Add("7d830ff0-3632-42d6-83ee-f2dee122c998", 1, {{"condition", 0.3}}, std::nullopt).outputs.at(0).id;
        fixture.inventory.TakeChanges();
        EQUALS(sharpen(stub, {{"condition", 0.9}}).code, std::string("invalidSharpen"));
        const auto rows = fixture.inventory.Rows();
        EQUALS(sharpen(fixture.sword, {{"quality", 3}, {"condition", 0.9}}).ok, false);
        EQUALS(sharpen(fixture.sword, {{"quality", 2}, {"condition", 0.2}}).ok, false);
        EQUALS(fixture.inventory.Sharpen({fixture.sword, {{"quality", 2}, {"condition", 0.5}}, {{"quality", 2}, {"condition", 0.9}}}).ok, false);
        EQUALS(fixture.inventory.Rows() == rows, true);
        EQUALS(fixture.inventory.TakeChanges().empty(), true);
        EQUALS(fixture.inventory.Sharpen({fixture.sword, before, {{"quality", 2}, {"condition", 0.9}}}).ok, true);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 0.9}}), 1U);
        EQUALS(fixture.Amount(InventoryRepairTests::kSword, {{"quality", 2}, {"condition", 0.3}}), 2U);
        EQUALS(fixture.inventory.TakeChanges().at(0).reason, std::string("sharpen"));
    });

    IT("reads a sharpening message only whole", {
        nlohmann::json message {{"request", "test"}, {"equipment", {{"id", "1"}, {"before", {{"health", 0.5}}}, {"after", {{"health", 0.8}}}}}, {"equipped", true}};
        std::string request;
        KCDC::Features::Inventory::InventoryRepairPart weapon;
        EQUALS(KCDC::Features::Inventory::InventoryGameplay::ReadSharpen(message, request, weapon), true);
        EQUALS(weapon.equipped, true);
        EQUALS(request, std::string("test"));
        message.erase("equipped");
        EQUALS(KCDC::Features::Inventory::InventoryGameplay::ReadSharpen(message, request, weapon), false);
        message["equipped"] = false;
        message["equipment"].erase("before");
        EQUALS(KCDC::Features::Inventory::InventoryGameplay::ReadSharpen(message, request, weapon), false);
    });
});
