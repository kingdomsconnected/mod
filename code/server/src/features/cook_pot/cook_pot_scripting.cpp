#include "cook_pot_scripting.h"
#include "core/scripting/script_context.h"
#include "core/server.h"
#include "features/inventory/inventory_scripting.h"
#include "features/player/player_scripting.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/virtual_world_arg.h"
#include <cmath>
#include <optional>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

namespace KCDC::Scripting {
    namespace {
        bool World(const v8::FunctionCallbackInfo<v8::Value> &info, int at, std::uint32_t &world) {
            world = MafiaNet::VIRTUAL_WORLD_DEFAULT;
            if (info.Length() <= at || info[at]->IsUndefined()) {
                return true;
            }
            // The global visibility sentinel is not a stock world.
            const auto value = info[at]->IsNumber() ? VirtualWorldFromNumber(v8pp::from_v8<double>(info.GetIsolate(), info[at])) : std::nullopt;
            if (!value.has_value() || *value == MafiaNet::VIRTUAL_WORLD_GLOBAL) {
                return false;
            }
            world = *value;
            return true;
        }

        void All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::uint32_t world;
            if (!World(info, 0, world)) {
                info.GetIsolate()->ThrowError("CookPot.all: expected an optional virtual world id");
                return;
            }
            info.GetReturnValue().Set(FrozenValue(info.GetIsolate(), Server::Get()->GetCookPots().All(world)));
        }

        bool Identity(const v8::FunctionCallbackInfo<v8::Value> &info, int worldIndex, std::uint64_t &guid, std::uint32_t &world) {
            if (info.Length() < 1 || !info[0]->IsString() || !World(info, worldIndex, world)) {
                info.GetIsolate()->ThrowError("CookPot: expected a level GUID and optional virtual world id");
                return false;
            }
            guid = ParseLevelGuid(v8pp::from_v8<std::string>(info.GetIsolate(), info[0]));
            return true;
        }

        void Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 1, guid, world)) {
                info.GetReturnValue().Set(FrozenValue(info.GetIsolate(), Server::Get()->GetCookPots().Snapshot(guid, world)));
            }
        }

        void Refill(const v8::FunctionCallbackInfo<v8::Value> &info) {
            double amount = KCDC::Shared::CookPot::Stock::kDefaultPortions;
            if (info.Length() > 1 && !info[1]->IsUndefined()) {
                if (!info[1]->IsNumber()) {
                    info.GetIsolate()->ThrowError("CookPot.refill: portions must be an integer from 0 to 1000");
                    return;
                }
                amount = v8pp::from_v8<double>(info.GetIsolate(), info[1]);
            }
            if (!std::isfinite(amount) || amount < 0 || amount > KCDC::Shared::CookPot::Stock::kMaximumPortions || amount != std::floor(amount)) {
                info.GetIsolate()->ThrowError("CookPot.refill: portions must be an integer from 0 to 1000");
                return;
            }
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 2, guid, world)) {
                info.GetReturnValue().Set(Server::Get()->GetCookPots().Refill(guid, world, static_cast<std::uint16_t>(amount)));
            }
        }

        void Enabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 2 || !info[1]->IsBoolean()) {
                info.GetIsolate()->ThrowError("CookPot.setEnabled: expected (guid, enabled, virtualWorld?)");
                return;
            }
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 2, guid, world)) {
                info.GetReturnValue().Set(Server::Get()->GetCookPots().SetEnabled(guid, world, info[1]->BooleanValue(info.GetIsolate())));
            }
        }

        void State(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::string state = info.Length() > 1 && info[1]->IsString() ? std::string(v8pp::from_v8<std::string>(info.GetIsolate(), info[1])) : std::string();
            if (state != "empty" && state != "half" && state != "full") {
                info.GetIsolate()->ThrowError("CookPot.setState: state must be empty, half or full");
                return;
            }
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 2, guid, world)) {
                info.GetReturnValue().Set(Server::Get()->GetCookPots().SetState(guid, world, state == "full" ? KCDC::Shared::CookPot::State::Full : state == "half" ? KCDC::Shared::CookPot::State::Half : KCDC::Shared::CookPot::State::Empty));
            }
        }
        void Poisons(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto items = nlohmann::json::array();
            for (const auto &entry : KCDC::Shared::Game::g_CookPotPoisons) {
                items.push_back(entry.item);
            }
            info.GetReturnValue().Set(FrozenValue(info.GetIsolate(), items));
        }

        void Food(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto name = info.Length() > 1 && info[1]->IsString() ? v8pp::from_v8<std::string>(info.GetIsolate(), info[1]) : std::string();
            if (name != "goulash" && name != "lentil" && name != "soup") {
                info.GetIsolate()->ThrowError("CookPot.setFood: food must be goulash, lentil or soup");
                return;
            }
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 2, guid, world)) {
                info.GetReturnValue().Set(Server::Get()->GetCookPots().SetFood(guid, world, name == "goulash" ? KCDC::Shared::Game::CookPotFood::Goulash : name == "lentil" ? KCDC::Shared::Game::CookPotFood::Lentil : KCDC::Shared::Game::CookPotFood::Soup));
            }
        }

        void FireLit(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 2 || !(info[1]->IsBoolean() || info[1]->IsNull())) {
                info.GetIsolate()->ThrowError("CookPot.setFireLit: expected (guid, lit, virtualWorld?)");
                return;
            }
            std::uint64_t guid;
            std::uint32_t world;
            if (Identity(info, 2, guid, world)) {
                const auto fire = info[1]->IsNull() ? KCDC::Shared::CookPot::Fire::Native : info[1]->BooleanValue(info.GetIsolate()) ? KCDC::Shared::CookPot::Fire::Burning : KCDC::Shared::CookPot::Fire::Out;
                info.GetReturnValue().Set(Server::Get()->GetCookPots().SetFire(guid, world, fire));
            }
        }

        void PlayerAction(const v8::FunctionCallbackInfo<v8::Value> &info, bool poison) {
            auto *player = info.Length() > 0 && info[0]->IsObject() ? v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info[0].As<v8::Object>()) : nullptr;
            if (player == nullptr || info.Length() < 3 || !info[1]->IsString() || (poison && !info[2]->IsString())) {
                info.GetIsolate()->ThrowError("CookPot: expected (player, guid, recipeOrPoisonRow)");
                return;
            }
            const auto guid   = ParseLevelGuid(v8pp::from_v8<std::string>(info.GetIsolate(), info[1]));
            const auto result = poison ? Server::Get()->GetCookPots().Poison(player->ResolveHuman(), guid, v8pp::from_v8<std::string>(info.GetIsolate(), info[2])) : Server::Get()->GetCookPots().RefillFromInventory(player->ResolveHuman(), guid, InventoryRequestArgument(info, 2));
            info.GetReturnValue().Set(FrozenValue(info.GetIsolate(), result));
        }

        void RefillFromInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            PlayerAction(info, false);
        }
        void Poison(const v8::FunctionCallbackInfo<v8::Value> &info) {
            PlayerAction(info, true);
        }
    } // namespace

    void RegisterCookPotBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &snapshot             = ServerCatalog().data_type("CookPotSnapshot", "A frozen snapshot of one stew pot in one virtual world.");
        const std::string foodType = "\"goulash\" | \"lentil\" | \"soup\"";
        snapshot.add_property("food", foodType, "Meal served by this pot, including its native nutrition and appearance.");
        snapshot.add_property("nutrition", "number", "Base nutrition per meal: goulash 25, lentil 10, soup 15.");
        snapshot.add_property("poison", "string | null", "Ingested buff GUID applied to future meals, or null. Visible to server scripts only.");
        snapshot.add_property("guid", "string", "The fireplace's level GUID.");
        snapshot.add_property("position", "{ readonly x: number; readonly y: number; readonly z: number }", "Position of the eating trigger.");
        snapshot.add_property("portions", "number", "Unreserved meals remaining.");
        snapshot.add_property("capacity", "number", "Portions in the most recent refill; sets the halfway point.");
        snapshot.add_property("enabled", "boolean", "Whether eating is allowed. Disabling preserves the contents.");
        snapshot.add_property("state", "\"empty\" | \"half\" | \"full\"", "Visible food level, derived from remaining portions.");
        snapshot.add_property("fireLit", "boolean | null", "Whether the fire burns for every player in this world, as set by CookPot.setFireLit; null while each player's own game lights and puts it out, as in single player.");
        snapshot.add_property("virtualWorld", "number", "Virtual world that owns this stock.");
        v8pp::module module(isolate, ServerCatalog(), "CookPot", "Server-owned stew pots. Stocked pots begin with four shared meals, with no automatic refill. State lasts until the server stops.");
        const auto guid  = v8pp::metadata::param("guid", "string", false, "Fireplace level GUID from CookPot.all().");
        const auto world = v8pp::metadata::param("virtualWorld", "number", true, "Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.");
        module.function("all", &All, v8pp::metadata::docs("CookPotSnapshot[]", {world}, "Lists every catalogued pot on the server's level in one virtual world; empty for a world no player or write has used yet."));
        module.function("get", &Get, v8pp::metadata::docs("CookPotSnapshot | null", {guid, world}, "Returns a snapshot, or null for an unknown pot."));
        module.function("refill", &Refill,
            v8pp::metadata::docs("boolean", {guid, v8pp::metadata::param("portions", "number", true, "Integer from 0 to 1000; defaults to four."), world}, "Replaces remaining stock and capacity and clears poison, preserving food and enabled. Returns false for an unknown pot."));
        module.function("setEnabled", &Enabled, v8pp::metadata::docs("boolean", {guid, v8pp::metadata::param("enabled", "boolean", false, "False blocks eating without changing stock or its appearance."), world}, "Allows or blocks eating. Returns false for an unknown pot."));
        module.function("setState", &State,
            v8pp::metadata::docs("boolean", {guid, v8pp::metadata::param("state", "\"empty\" | \"half\" | \"full\"", false, "Empty, two portions, or four portions; resets capacity to four."), world},
                "Changes food level and stock together, preserving food, poison and enabled. Returns false for an unknown pot."));
        auto &recipe = ServerCatalog().data_type("CookPotRecipe", "Server-defined ingredients and portions for refilling an empty pot.");
        recipe.add_property("food", foodType, "Meal to put in the pot.");
        recipe.add_property("portions", "number", "Integer from 1 to 1000.");
        recipe.add_property("ingredients", "{ item: string; amount: number }[]", "One to sixteen food item classes by name or GUID, with positive integer amounts. Split stacks are combined; spoiled rows are skipped.");
        auto &result = ServerCatalog().data_type("CookPotResult", "An atomic pot action. A refusal changes neither items nor contents.");
        result.add_property("ok", "boolean", "Whether the action succeeded.");
        result.add_property("code", "string", "ok, invalid_player, unknown_pot, disabled, too_far, cannot_act, inventory_not_ready, not_empty, invalid_recipe, missing_ingredients, empty, already_poisoned, unknown_item, not_poison, or an inventory refusal code.");
        const auto player = v8pp::metadata::param("player", "Player", false, "Connected player within four metres; their current virtual world selects the pot.");
        module.function("poisons", &Poisons, v8pp::metadata::docs("string[]", {}, "Item class GUIDs accepted by CookPot.poison, including each native strength variant."));
        module.function("setFood", &Food,
            v8pp::metadata::docs("boolean", {guid, v8pp::metadata::param("food", foodType, false, "Native meal type."), world}, "Changes future meals and appearance, preserving stock, poison and enabled. Already reserved meals keep their previous type."));
        module.function("setFireLit", &FireLit,
            v8pp::metadata::docs("boolean", {guid, v8pp::metadata::param("lit", "boolean | null", false, "True lights the fire, false puts it out, null hands it back to each player's own game."), world},
                "Lights or puts out the pot's fire, with its steam and smoke, for every player in that world. From then on the fireplace AI no longer changes it: villagers stoking it and its burn-out timer leave it as set, until null hands it back. Returns false for an unknown pot."));
        module.function("refillFromInventory", &RefillFromInventory,
            v8pp::metadata::docs("CookPotResult", {player, guid, v8pp::metadata::param("recipe", "CookPotRecipe", false, "Ingredients and yield chosen by this server script.")},
                "Spends ingredients atomically and fills an empty enabled pot. Clears old poison. Validates player, distance, ability to act, world and synchronized inventory. Never pass unvalidated client recipe data."));
        module.function("poison", &Poison,
            v8pp::metadata::docs("CookPotResult", {player, guid, v8pp::metadata::param("rowId", "string", false, "Inventory row containing a native Poison item; spends exactly one.")},
                "Poisons a stocked enabled pot with the item's ingested effect. Refuses a second poison until refill. A meal takes the poison present when reserved, applied once on its eating receipt; NPC poisoning and native crime reactions are not included."));
        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "CookPot"), module.new_instance()).Check();
    }
} // namespace KCDC::Scripting
