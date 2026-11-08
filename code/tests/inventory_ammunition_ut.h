#pragma once

#include "features/inventory/inventory_gameplay.h"

#include <map>
#include <set>
#include <vector>

MODULE(inventory_ammunition, {
    IT("keeps an equipped quiver for each ammunition type", {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        const std::vector<KCDC::Features::Inventory::InventoryRow> rows {
            {"1", "dfea5d01-b25c-414a-9ab4-6911a5f82118", 20}, // Crude arrows.
            {"2", "710e3706-8974-404b-b23a-6f51670ef1ed", 30}, // Hunting arrows.
            {"3", "40337bef-e965-4a60-abee-695e9a784fa4", 5},  // Hunting bolts.
            {"4", "fb30c64e-2360-4ed7-b805-531b3424fe4d", 6},  // Balls.
            {"5", "b302ee9d-d64c-4dc2-b045-05cd7d238eb4", 7},  // Scatter ammunition.
        };
        EQUALS(inventory.Set(rows, std::nullopt).ok, true);
        const auto equipped = KCDC::Features::Inventory::InventoryGameplay::CountEquipment(inventory, {{"1", 20}, {"2", 30}, {"3", 5}, {"4", 6}, {"5", 7}});
        const std::map<std::string, std::uint32_t> expected {{"2", 30}, {"3", 5}, {"4", 6}, {"5", 7}};
        EQUALS(equipped, expected);
    });
    IT("spends and recovers a bolt while a fuller arrow quiver is equipped", {
        for (const std::uint32_t amount : {1U, 3U}) {
            KCDC::Features::Inventory::InventoryIds ids;
            KCDC::Features::Inventory::Inventory inventory(ids);
            const nlohmann::json properties {{"poison", "03845da1-2125-45dd-a315-cf7dba569e54"}, {"poisonCharges", 1}};
            EQUALS(inventory.Set({{"1", "dfea5d01-b25c-414a-9ab4-6911a5f82118", 20}, {"2", "40337bef-e965-4a60-abee-695e9a784fa4", amount, properties}}, std::nullopt).ok, true);
            const auto equipped = KCDC::Features::Inventory::InventoryGameplay::CountEquipment(inventory, {{"1", 20}, {"2", amount}});
            std::set<std::string> loaded;
            for (const auto &[id, count] : equipped) {
                loaded.insert(id);
            }
            std::vector<KCDC::Features::Inventory::InventoryRow> spent;
            const auto fired = KCDC::Features::Inventory::InventoryGameplay::Consume(inventory, KCDC::Features::Inventory::InventoryConsumption::Shot, loaded, {{"2", 1}}, &spent);
            EQUALS(fired.ok, true);
            if (!fired.ok) {
                continue;
            }
            EQUALS(inventory.Rows().at("1").amount, 20U);
            EQUALS(inventory.Rows().contains("2"), amount > 1);
            if (amount > 1) {
                EQUALS(inventory.Rows().at("2").amount, amount - 1);
            }
            EQUALS(spent.size(), 1U);
            if (spent.size() != 1) {
                continue;
            }
            KCDC::Features::Inventory::SpentRounds rounds;
            rounds.Fire(spent.front(), spent.front().amount, 7, 1000);
            EQUALS(KCDC::Features::Inventory::SpentRounds::LandsAs(spent.front().itemClass, "a1bbf4df-3242-498e-9595-7879665c4ee8"), true);
            const auto recovered = rounds.Claim(spent.front().itemClass, 7, 2000);
            EQUALS(recovered.has_value(), true);
            if (recovered) {
                EQUALS(recovered->itemClass, spent.front().itemClass);
                EQUALS(recovered->metadata, spent.front().metadata);
            }
            EQUALS(rounds.Claim(spent.front().itemClass, 7, 2001).has_value(), false);
        }
    });
});
