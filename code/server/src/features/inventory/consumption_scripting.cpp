#include "consumption_scripting.h"
#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "inventory_scripting.h"
#include "shared/features/player/player_stat_bindings.h"
#include "shared/game/consumable_catalog.h"
#include "shared/scripting/catalog.h"
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    void DescribeConsumable(nlohmann::json &consumption) {
        const auto *entry   = Shared::Game::FindConsumable(consumption.at("item").get<std::string>());
        consumption["kind"] = entry != nullptr && entry->potion ? "potion" : "food";
        consumption["baseEffects"] =
            entry == nullptr ? nlohmann::json(nullptr) : nlohmann::json {{"buff", entry->buff.empty() ? nlohmann::json(nullptr) : nlohmann::json(entry->buff)}, {"health", entry->health}, {"energy", entry->energy}, {"nourishment", entry->nourishment}, {"alcohol", entry->alcohol}};
    }
    bool PlayerConsuming(std::uint64_t player, const nlohmann::json &consumption) {
        return EmitReservedEventSync("playerConsuming", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, consumption)};
        });
    }

    bool PlayerConsumptionEffects(std::uint64_t player, const nlohmann::json &consumption) {
        return EmitReservedEventSync("playerConsumptionEffects", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, consumption)};
        });
    }

    bool PlayerPoisonAbsorbing(std::uint64_t player, const nlohmann::json &consumption, const std::string &buff) {
        return EmitReservedEventSync("playerPoisonAbsorbing", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, consumption), v8pp::to_v8(isolate, buff)};
        });
    }

    bool PlayerPreparing(std::uint64_t player, const nlohmann::json &preparation) {
        return EmitReservedEventSync("playerPreparing", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, preparation)};
        });
    }

    void PlayerConsumed(std::uint64_t player, const nlohmann::json &consumption) {
        EmitReservedEvent("playerConsumed", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, consumption)};
        });
    }

    void RegisterConsumptionEvents() {
        ServerCatalog()
            .data_type("EventMap")
            .add_property("playerStatsChanged", "[player: Player, changes: { stat: " + PlayerStatType(Shared::Game::kPlayerStats) + " | " + PlayerStatType(Shared::Game::kDerivedPlayerStats) + "; previous: number; current: number }[]]",
                "After the server receives changed player stats, including poison strength, drunkenness, health loss and potion-related readings. Changes are snapshots batched per server tick. The first valid report and each new body establish a baseline. These are observations, not cancellable requests; the client can coalesce intermediate values. No cause is inferred. Use consumption vetoes and custom server effects to decide gameplay before it happens.");
        auto &description = ServerCatalog().data_type("Consumption", "A server-validated serving. Item properties and pot poison come from server state; changing the snapshot does not change the decision.");
        description.add_property("source", "\"inventory\" | \"cookPot\"", "Where the serving comes from.");
        description.add_property("item", "string", "Native food or potion item class GUID.");
        description.add_property("kind", "\"food\" | \"potion\"", "Native food type; potions use the game's food type 1.");
        description.add_property("baseEffects", "{ buff: string | null; health: number; energy: number; nourishment: number; alcohol: number } | null",
            "Native table inputs before item condition, spoilage, perks or soul modifiers. Health effects may run over time. These are not promised final stat deltas. Null when the catalog has no row.");
        description.add_property("rowId", "string | null", "Inventory row, or null for a pot meal.");
        description.add_property("potGuid", "string | null", "Cooking pot GUID, or null for an inventory item.");
        description.add_property("metadata", "Record<string, unknown>", "Server item properties, including condition. Empty for a pot meal.");
        description.add_property("poison", "string | null", "Additional cooking pot poison buff GUID, or null. Spoilage and the item's own effects are part of nativeEffects.");
        description.add_property("nativeEffects", "boolean", "Whether native nutrition, energy, alcohol, potion and spoilage effects are allowed. Final in playerConsumed.");
        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerConsuming", "[player: Player, consumption: Consumption]",
            "Before reserving a food or potion unit or pot serving. Return false synchronously to refuse without spending stock or applying effects. Async handlers cannot veto. Do not apply replacement effects here: native use can still fail.");
        events.add_property("playerConsumptionEffects", "[player: Player, consumption: Consumption]",
            "After playerConsuming accepts, before reservation. Return false synchronously to consume without native nutrition, energy, alcohol, potion, spoilage or added pot poison effects. Apply custom effects in playerConsumed using player.setStat, addBuff and clearBuffs.");
        events.add_property("playerPoisonAbsorbing", "[player: Player, consumption: Consumption, buff: string]",
            "Before applying a reserved pot meal's additional poison on its eating receipt. Return false synchronously to suppress it. Runs only when nativeEffects is true. Native food spoilage and item buffs can be replaced through playerConsumptionEffects and Buffs.claim.");
        events.add_property("playerConsumed", "[player: Player, consumption: Consumption]",
            "Once after the owning client confirms successful use. Use for custom stat changes and buffs after suppressing native effects. Missing receipts and expired or replaced bodies never raise it. Inventory uses also raise playerItemUsed.");

        auto &preparation = ServerCatalog().data_type("Preparation", "Items a player's game dried on a drying rack, smoked in a smokehouse or cooked at a fire, and what the server is about to make of them. Frozen: a handler can refuse it, not change it.");
        preparation.add_property("kind", "\"dry\" | \"smoke\" | \"cook\"", "A drying rack dries herbs and food; a smokehouse smokes food; a fire cooks food.");
        preparation.add_property("station", "string", "The drying rack, smokehouse or fire the player stands at, as `<level>:<guid>`; the same on every machine and across restarts. A fire's guid is its cook pot's, as `CookPot.get` takes it.");
        preparation.add_property("items", "{ id: string; item: string; amount: number; product: string }[]", "Each inventory row about to be taken: `amount` units of class `item`, which become class `product`.");
        events.add_property("playerPreparing", "[player: Player, preparation: Preparation]",
            "Dispatched when a player's game dried, smoked or cooked items, before the server converts them in its inventory. The game converts at once when its animation ends; nothing about it takes time. Return `false` from a handler to refuse it: the items stay as they were, and the player's game puts them back. Every handler runs, synchronously: handler promises are not awaited, so an async handler cannot refuse. "
            "To make it take time, refuse it, take the rows with `Inventory.remove` and give the products with `Inventory.add` when your timer is up. The inventory change is `dry`, `smoke` or `cook`. Only an accepted one earns the game's survival XP, which the server grants per unit converted as `player.addXp` grants XP: the player's own multipliers apply; `playerXpGaining`, `Progression.setXpRate`, `setXpBudget`, `setLevelCap` and `setNativeXp(false)` do not.");
    }

    void PlayerStatsChanged(std::uint64_t player, const nlohmann::json &changes) {
        EmitReservedEvent("playerStatsChanged", [&](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, player), FrozenValue(isolate, changes)};
        });
    }
} // namespace KCDC::Scripting
