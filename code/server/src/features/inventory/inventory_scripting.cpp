#include "inventory_scripting.h"
#include "consumption_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_scripting.h"
#include "features/player/player_roster.h"
#include "features/player/player_scripting.h"
#include "shared/game/custom_item_registry.h"
#include "shared/game/item_classes.h"
#include "shared/game/item_stats.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/inventory_property_policy.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <utils/string_utils.h>

#include <cmath>
#include <stdexcept>

namespace KCDC::Scripting {
    namespace {
        // A whole inventory restored at once is the largest request.
        constexpr std::size_t kMaximumRequestBytes = 1024 * 1024;
    } // namespace

    nlohmann::json InventoryRequestArgument(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        v8::Local<v8::String> encoded;
        if (info.Length() <= index || !info[index]->IsObject() || !v8::JSON::Stringify(info.GetIsolate()->GetCurrentContext(), info[index]).ToLocal(&encoded)) {
            throw std::runtime_error("Inventory requires a request object");
        }
        const auto body = v8pp::from_v8<std::string>(info.GetIsolate(), encoded);
        if (body.size() > kMaximumRequestBytes) {
            throw std::runtime_error("Inventory request is too large");
        }
        // Stringify produced it, so it parses; a lone surrogate is the one
        // input that does not, and it is a bad request like any other.
        return nlohmann::json::parse(body, nullptr, false);
    }

    v8::Local<v8::Value> InventoryValue(v8::Isolate *isolate, const nlohmann::json &value) {
        v8::Local<v8::Value> result;
        if (!v8::JSON::Parse(isolate->GetCurrentContext(), v8pp::to_v8(isolate, value.dump()).As<v8::String>()).ToLocal(&result)) {
            return v8::Null(isolate);
        }
        return result;
    }

    namespace {
        // The value is the server's own JSON, parsed fresh; the bound keeps
        // the recursion off a deep graph.
        constexpr std::size_t kMaximumFrozenDepth = 64;

        bool Freeze(v8::Local<v8::Context> context, v8::Local<v8::Value> value, std::size_t depth) {
            if (!value->IsObject()) {
                return true;
            }
            if (depth >= kMaximumFrozenDepth) {
                return false;
            }
            const auto object = value.As<v8::Object>();
            v8::Local<v8::Array> names;
            if (!object->GetOwnPropertyNames(context).ToLocal(&names)) {
                return false;
            }
            for (std::uint32_t index = 0; index < names->Length(); ++index) {
                v8::Local<v8::Value> key, child;
                if (!names->Get(context, index).ToLocal(&key) || !object->Get(context, key).ToLocal(&child) || !Freeze(context, child, depth + 1)) {
                    return false;
                }
            }
            return object->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false);
        }
    } // namespace

    v8::Local<v8::Value> FrozenValue(v8::Isolate *isolate, const nlohmann::json &value) {
        const auto result = InventoryValue(isolate, value);
        if (!Freeze(isolate->GetCurrentContext(), result, 0)) {
            return v8::Null(isolate);
        }
        return result;
    }

    namespace {
        void Classes(const v8::FunctionCallbackInfo<v8::Value> &info) {
            nlohmann::json rows = nlohmann::json::array();
            Shared::Game::ForEachItemClass([&](const Shared::Game::ItemClassEntry &item) {
                const auto *properties = Shared::Game::FindInventoryPropertyClass(item.guid);
                if (!properties || !properties->equippable) {
                    return;
                }
                rows.push_back({{"id", item.guid}, {"name", item.name}, {"nameKey", item.nameKey}, {"category", item.category}, {"maximumQuality", properties->maximumQuality}, {"quest", properties->questItem}});
            });
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), rows));
        }

        struct InventoryOwner {
            std::uint64_t player = 0;
            std::uint64_t npc    = 0;
            KCDC::Features::Inventory::Inventory *Stock(KCDC::Features::Inventory::InventoryService &service) const {
                auto *stock = npc != 0 ? KCDC::Features::Npc::NpcInventoryModule::Get().Find(npc) : nullptr;
                return npc == 0 ? service.Find(player) : stock != nullptr && stock->Ready() ? &stock->Stock() : nullptr;
            }
            void Changed() const {
                if (npc != 0) {
                    KCDC::Features::Npc::NpcInventoryModule::Get().Changed(npc);
                }
            }
        };

        InventoryOwner OwnerAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            auto *player = info.Length() > index ? v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info[index]) : nullptr;
            if (player != nullptr) {
                return {player->GetId(), 0};
            }
            auto *npc = info.Length() > index ? v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info[index]) : nullptr;
            if (npc != nullptr) {
                return {0, npc->GetId()};
            }
            throw std::runtime_error("Inventory requires a Player or Npc");
        }

        template <typename Call>
        void Answer(const v8::FunctionCallbackInfo<v8::Value> &info, Call &&call) {
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), call(Server::Get()->GetInventory())));
        }

        void Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner = OwnerAt(info, 0);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                return owner.npc != 0 ? KCDC::Features::Npc::NpcInventoryModule::Get().ScriptGet(owner.npc) : inventory.ScriptGet(owner.player);
            });
        }

        void Add(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner   = OwnerAt(info, 0);
            const auto request = InventoryRequestArgument(info, 1);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                const auto result = owner.npc != 0 ? inventory.ScriptAdd(owner.Stock(inventory), request) : inventory.ScriptAdd(owner.player, request);
                owner.Changed();
                return result;
            });
        }

        void Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner   = OwnerAt(info, 0);
            const auto request = InventoryRequestArgument(info, 1);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                const auto result = owner.npc != 0 ? inventory.ScriptRemove(owner.Stock(inventory), request) : inventory.ScriptRemove(owner.player, request);
                owner.Changed();
                return result;
            });
        }

        void UpdateCustom(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner   = OwnerAt(info, 0);
            const auto request = InventoryRequestArgument(info, 1);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                return inventory.ScriptUpdateCustom(owner.player, request);
            });
        }

        void RegisterItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 2 || !info[0]->IsString()) {
                throw std::invalid_argument("Items.register requires an id and definition");
            }
            const auto definition = InventoryRequestArgument(info, 1);
            const auto &item      = KCDC::Shared::Game::CustomItemRegistry::Get().Register(v8pp::from_v8<std::string>(info.GetIsolate(), info[0]), definition);
            Server::Get()->GetInventory().PublishDefinitions();
            info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), item.guid));
        }

        void SetProperties(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner   = OwnerAt(info, 0);
            const auto request = InventoryRequestArgument(info, 1);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                const auto result = owner.npc != 0 ? inventory.ScriptSetProperties(owner.Stock(inventory), request) : inventory.ScriptSetProperties(owner.player, request);
                owner.Changed();
                return result;
            });
        }

        void Set(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner   = OwnerAt(info, 0);
            const auto request = InventoryRequestArgument(info, 1);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                return owner.npc != 0 ? KCDC::Features::Npc::NpcInventoryModule::Get().ScriptSet(owner.npc, request) : inventory.ScriptSet(owner.player, request);
            });
        }

        void Transfer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto source  = OwnerAt(info, 0);
            const auto target  = OwnerAt(info, 1);
            const auto request = InventoryRequestArgument(info, 2);
            Answer(info, [&](KCDC::Features::Inventory::InventoryService &inventory) {
                const auto result = inventory.ScriptTransfer(source.Stock(inventory), target.Stock(inventory), request);
                source.Changed();
                target.Changed();
                return result;
            });
        }

        const KCDC::Shared::Game::ItemClassEntry *ItemClassAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsString()) {
                throw std::runtime_error("Inventory requires an item class GUID or name");
            }
            return KCDC::Features::Player::Players::ResolveItemClass(v8pp::from_v8<std::string>(info.GetIsolate(), info[index]));
        }

        void GetItemPrice(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto *item              = ItemClassAt(info, 0);
            const nlohmann::json metadata = info.Length() > 1 && !info[1]->IsUndefined() ? InventoryRequestArgument(info, 1) : nlohmann::json::object();
            KCDC::Shared::Inventory::ItemUnitPrices prices;
            std::string error;
            if (item == nullptr || !KCDC::Shared::Inventory::CalculateItemUnitPrices(item->guid, metadata, prices, error)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), {{"pristine", prices.pristine}, {"current", prices.current}}));
        }

        void SetItemName(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto *item = ItemClassAt(info, 0);
            if (item == nullptr) {
                throw std::runtime_error("Inventory.setItemName: no such item class");
            }
            if (info.Length() < 2 || !(info[1]->IsString() || info[1]->IsNull())) {
                throw std::runtime_error("Inventory.setItemName requires a name, or null");
            }
            const std::string name = info[1]->IsNull() ? std::string() : v8pp::from_v8<std::string>(info.GetIsolate(), info[1]);
            if (!Server::Get()->GetInventory().SetItemName(*item, name)) {
                throw std::runtime_error(item->nameKey.empty() ? "Inventory.setItemName: the game shows no name for this class" : "Inventory.setItemName: a name is up to 128 bytes with no control characters");
            }
            nlohmann::json renamed = nlohmann::json::array();
            KCDC::Shared::Game::ForEachItemClass([&](const KCDC::Shared::Game::ItemClassEntry &entry) {
                if (Framework::Utils::StringUtils::EqualsIgnoreCase(entry.nameKey, item->nameKey)) {
                    renamed.push_back(std::string(entry.guid));
                }
            });
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), renamed));
        }

        void SetItemStats(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const auto *item     = ItemClassAt(info, 0);
            if (item == nullptr) {
                throw std::runtime_error("Inventory.setItemStats: no such item class");
            }
            if (!KCDC::Shared::Game::ItemCategoryHasStats(item->category)) {
                throw std::runtime_error("Inventory.setItemStats: '" + std::string(item->name) + "' is a " + std::string(item->category) + ", which carries no attack, defense or requirement");
            }
            if (info.Length() < 2 || !(info[1]->IsObject() || info[1]->IsNull())) {
                throw std::runtime_error("Inventory.setItemStats requires the stats as an object, or null");
            }
            std::vector<KCDC::Shared::RPC::ItemStats::Value> values;
            if (info[1]->IsObject()) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Object> stats    = info[1].As<v8::Object>();
                v8::Local<v8::Array> keys;
                if (!stats->GetOwnPropertyNames(context).ToLocal(&keys)) {
                    throw std::runtime_error("Inventory.setItemStats requires the stats as an object");
                }
                for (std::uint32_t i = 0; i < keys->Length(); ++i) {
                    v8::Local<v8::Value> key;
                    v8::Local<v8::Value> value;
                    if (!keys->Get(context, i).ToLocal(&key) || !stats->Get(context, key).ToLocal(&value)) {
                        continue;
                    }
                    const std::string name = v8pp::from_v8<std::string>(isolate, key);
                    const int stat         = KCDC::Shared::Game::FindItemStat(name);
                    if (stat < 0) {
                        throw std::runtime_error("Inventory.setItemStats: '" + name + "' is not a stat; see the Inventory.setItemStats documentation for the names");
                    }
                    if (!KCDC::Shared::Game::ItemStatAppliesTo(item->category, static_cast<KCDC::Shared::Game::ItemStat>(stat))) {
                        throw std::runtime_error("Inventory.setItemStats: a " + std::string(item->category) + " has no '" + name + "'");
                    }
                    if (value->IsUndefined() || value->IsNull()) {
                        continue;
                    }
                    if (!value->IsNumber()) {
                        throw std::runtime_error("Inventory.setItemStats: '" + name + "' must be a number");
                    }
                    const double number = value.As<v8::Number>()->Value();
                    if (!std::isfinite(number) || std::fabs(number) > KCDC::Shared::Game::kItemStatLimit) {
                        throw std::runtime_error("Inventory.setItemStats: '" + name + "' must be a finite number within 100000 of zero");
                    }
                    if (!KCDC::Shared::Game::ItemStatAccepts(static_cast<KCDC::Shared::Game::ItemStat>(stat), static_cast<float>(number))) {
                        throw std::runtime_error("Inventory.setItemStats: '" + name + "' is a requirement and cannot be negative");
                    }
                    values.push_back({static_cast<std::uint8_t>(stat), static_cast<float>(number)});
                }
            }
            info.GetReturnValue().Set(Server::Get()->GetInventory().SetItemStats(*item, values));
        }

        void GetItemStats(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const auto *item     = ItemClassAt(info, 0);
            if (item == nullptr) {
                throw std::runtime_error("Inventory.getItemStats: no such item class");
            }
            const auto *values = Server::Get()->GetInventory().ItemStatsOf(*item);
            if (values == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> out      = v8::Object::New(isolate);
            for (const auto &value : *values) {
                out->Set(context, v8pp::to_v8(isolate, std::string(KCDC::Shared::Game::ItemStatName(static_cast<KCDC::Shared::Game::ItemStat>(value.stat)))), v8pp::to_v8(isolate, static_cast<double>(value.value))).Check();
            }
            info.GetReturnValue().Set(out);
        }
    } // namespace

    void GetPlayerInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *player = v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
        info.GetReturnValue().Set(player == nullptr ? v8::Local<v8::Value>(v8::Null(info.GetIsolate())) : InventoryValue(info.GetIsolate(), Server::Get()->GetInventory().ScriptGet(player->GetId())));
    }

    void GetNpcInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *npc = v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info.This());
        info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), npc != nullptr ? KCDC::Features::Npc::NpcInventoryModule::Get().ScriptGet(npc->GetId()) : nlohmann::json(nullptr)));
    }

    void EmitPlayerInventoryChanged(std::uint64_t playerId, const nlohmann::json &change) {
        EmitReservedEvent("playerInventoryChanged", [playerId, &change](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), InventoryValue(isolate, change)};
        });
    }

    void EmitPlayerInventoryReady(std::uint64_t playerId) {
        EmitReservedEvent("playerInventoryReady", [playerId](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId)};
        });
    }

    void EmitPlayerCustomItemUse(std::uint64_t playerId, const nlohmann::json &row, std::uint64_t revision) {
        EmitReservedEvent("playerCustomItemUse", [playerId, &row, revision](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), FrozenValue(isolate, row), v8pp::to_v8(isolate, revision)};
        });
    }

    void EmitPlayerItemUsed(std::uint64_t playerId, const std::string &itemClass, const char *kind, std::uint32_t amount) {
        EmitReservedEvent("playerItemUsed", [playerId, &itemClass, kind, amount](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), v8pp::to_v8(isolate, itemClass), v8pp::to_v8(isolate, kind), v8pp::to_v8(isolate, amount)};
        });
    }

    void RegisterInventoryBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        RegisterConsumptionEvents();
        auto &animal = ServerCatalog().data_type("AnimalLootItem", "One candidate from the game's animal loot preset; probability and inventory population belong to the gamemode.");
        animal.add_property("item", "string", "Item class GUID.");
        animal.add_property("amount", "number", "Maximum authored unit count.");
        animal.add_property("fraction", "number", "Authored quantity fraction, not a drop probability.");
        animal.add_property("variation", "number", "Uniform variation around the quantity fraction.");
        animal.add_property("food", "boolean", "The item is food.");
        animal.add_property("harvester", "boolean", "Tagged as a special Harvester part in the game data. This is metadata; the gamemode decides eligibility.");

        auto &unit = ServerCatalog().data_type("InventoryUnit", "A count of units from one row.");
        unit.add_property("id", "string", "The row.");
        unit.add_property("amount", "number", "How many of its units.");

        auto &row = ServerCatalog().data_type("InventoryRow", "A stack of one item class with one set of properties. Two rows never hold the same class with equal properties unless a restore put them there. Stackable custom rows merge only with matching properties. Nonstackable custom rows each contain one unit.");
        row.add_property("id", "string", "The row's identity within its owner's inventory. Stable while the row exists; save it with the row and pass it back to `Inventory.set`.");
        row.add_property("item", "string", "Item class GUID.");
        row.add_property("name", "string", "Internal table name, or Items.register id for a custom type. Display overrides are in metadata.custom.name.");
        row.add_property("amount", "number", "Units in the row.");
        row.add_property("quality", "number", "Quality, from 1 up to the class's maximum. Same as `metadata.quality`.");
        row.add_property("health", "number", "Absolute item health. Same as `metadata.health`.");
        row.add_property("condition", "number", "Health as a fraction of what this quality allows, 0 to 1. Same as `metadata.condition`.");
        row.add_property("equipped", "number", "Units the body wears or holds: reported by a player, authored stock for an NPC. Reads include them; pass them to `Inventory.set` to dress a restored body.");
        row.add_property("metadata", "Record<string, unknown>",
            "Every property the server keeps: `quality`, `health`, `condition`, and on missiles `poison` (a buff GUID) and `poisonCharges`, and `onEquipBuffs` (buff GUIDs). Custom items also carry `custom: { name?, description?, data? }`. Data stays server-side. A default is left out. Pass it back as it is to keep an item exactly.");

        auto &state = ServerCatalog().data_type("InventoryState", "One player's or NPC's inventory as the server holds it.");
        state.add_property("ready", "boolean", "For a player, their game shows this revision. For an NPC, its initial stock has been established. Wait for `playerInventoryReady` or `npcInventoryReady` respectively.");
        state.add_property("revision", "number", "Advances by one with every change. Pass it as a request's `revision` to refuse the request if anything changed in between.");
        state.add_property("items", "InventoryRow[]", "Every row.");
        state.add_property("harvested", "boolean | undefined", "NPC stock only: the carcass has already been harvested. Pass it to Inventory.set when restoring a corpse; absent preserves its existing state.");
        state.add_property("quickslots", "InventoryQuickslots | null", "The current native outfit's quick-access assignments and selections, using row IDs. Null until reported or restored. Save with items and pass both to Inventory.set.");

        auto &quickslots = ServerCatalog().data_type("InventoryQuickslots", "The current outfit's weapon and item quickslots. Indices are zero-based; each array has four entries. Empty references are null.");
        quickslots.add_property("weapons", "{ primary: string | null; secondary: string | null }[]", "Four weapon slots, each with primary and secondary row IDs. A row may appear in several slots.");
        quickslots.add_property("items", "(string | null)[]", "Four item slots, containing row IDs or null. Save equipped belts and pouches with the inventory rows to retain slot capacity.");
        quickslots.add_property("activeWeapon", "number", "Selected weapon slot, 0 through 3.");
        quickslots.add_property("activeItem", "number", "Selected item slot, 0 through 3.");

        auto &change = ServerCatalog().data_type("InventoryChange", "What one operation did to one inventory.");
        change.add_property("revision", "number", "The revision the operation produced.");
        change.add_property("reason", "string",
            "`add`, `remove`, `properties`, `set`, `transfer`, `wear` when the player's game wore an item down in use, `use` for food, potions and ointments, `shot` for a fired round, `pickpocket`, `trade` for a vendor deal, `repair` for a repair kit, `sharpen` for a grindstone, `dry`, `smoke` and `cook` for a drying rack, a smokehouse and a fire, `drop` for `player.dropInventory`, `loot`, `npcDeposit`, `harvest`, `outfit`, or the ground, stash and gathering reasons. `equipment` reports an equipment or quickslot change: items is empty, the item revision does not advance, and Inventory.get returns the new snapshot.");
        change.add_property("items", "{ id: string; before: InventoryRow | null; after: InventoryRow | null }[]", "Only the rows that changed. `before: null` is a new row and `after: null` an emptied one.");

        auto &result = ServerCatalog().data_type("InventoryResult", "What an inventory operation did.");
        result.add_property("ok", "boolean", "It happened. Nothing changes when it did not.");
        result.add_property("code", "string",
            "Empty on success, otherwise why not: `inventoryUnavailable`, `invalidRequest`, `staleRevision`, `invalidItem`, `invalidItems`, `invalidAmount`, `unknownItem`, `insufficientItems`, `inventoryCapacity`, `sameInventory`, `invalidEquipment`, or a property policy code (`invalidMetadata`, `unknownItemClass`, `questItem`, `invalidQuality`, `immutableItemHealth`, `invalidItemHealth`, `contradictoryItemHealth`, `invalidCreationSentinel`, `unsupportedPoisonProperties`, `unsupportedOnEquipBuffs`).");
        result.add_property("revision", "number", "The revision after the operation; for a transfer, the source's. 0 when refused.");
        result.add_property("items", "InventoryUnit[]", "The rows that received units: the row an `add` merged into or created, a transfer's rows in the target, every row a `set` wrote. Empty for `remove`.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerCustomItemUse", "[player: Player, item: InventoryRow, revision: number]", "A validated custom-item action. The frozen row includes server-only data. Scripts decide what happens and explicitly consume items.");
        events.add_property("playerInventoryChanged", "[player: Player, change: InventoryChange]",
            "An inventory changed. Raised on the next server tick in the order the changes happened, once per inventory an operation touched: a transfer raises it for both players. A handler may change inventories; those changes arrive on a later tick. Every change of a player who disconnects is raised before `playerDisconnect`. Save from here to keep inventories across sessions -- the server keeps nothing itself.");
        events.add_property("playerItemUsed", "[player: Player, item: string, kind: \"food\" | \"potion\" | \"ointment\" | \"shot\", amount: number]",
            "The player's game spent items: ate, drank, applied or fired them. Raised after the `playerInventoryChanged` that removed them. `item` is the class GUID.\n\nThis is the player's own game saying so: the server checked that the units existed and were of a kind that can be spent that way, and took them, but did not see the meal or the shot. A handler that rewards a use -- a heal, a buff -- can be had for the price of the item.");
        events.add_property("playerInventoryReady", "[player: Player]", "A player's game shows their inventory for the first time this session. Their inventory exists from `playerConnect`; restore a saved one there with `Inventory.set`, and it is what they load in with.");

        v8pp::module inventory(isolate, ServerCatalog(), "Inventory",
            "Player and NPC inventories. The server holds the stock and clients project it. Native NPC default stock is censused once by its elected simulator; `Inventory.set` establishes scripted stock immediately. Other NPC writes require ready stock. Every operation happens at once and in full, or not at all. `revision` is optional everywhere: give the one you read to refuse the operation when the inventory changed since.");
        inventory.function("classes", &Classes, v8pp::metadata::docs("{ id: string; name: string; nameKey: string; category: string; maximumQuality: number; quest: boolean }[]", {}, "Lists every equippable item the server knows: the game's own and those the running resources' KCD2 mods add through their item table patches. Names are table identifiers; nameKey is the native localization key. Maximum quality is the same bound Inventory.add enforces."));
        inventory.function("get", &Get, v8pp::metadata::docs("InventoryState | null", {v8pp::metadata::param("owner", "Player | Npc")}, "Reads a player's or NPC's inventory.", "A copy, or null for an owner that no longer exists."));
        inventory.function("add", &Add,
            v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("owner", "Player | Npc"), v8pp::metadata::param("request", "{ item: string; amount: number; metadata?: Record<string, unknown>; revision?: number; notify?: boolean }")},
                "Gives an owner items of a class, 1 to 10000 at a time. `item` is the class GUID or the exact name the game's own item tables use, the same spelling `giveItem` takes. Left-out properties mean quality 1 at full condition; `metadata.quality` asks for a higher tier, up to what the class is made in, and `invalidQuality` refuses one past it. The units join the row that already holds this class with these properties, if there is one. A player's game announces them with its own \"You received\" toast unless `notify` is false."));
        inventory.function("remove", &Remove,
            v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("owner", "Player | Npc"), v8pp::metadata::param("request", "{ units: InventoryUnit[]; revision?: number; notify?: boolean }")},
                "Takes exact units from named rows, every one of them or none. A player's game announces the loss with its own toast unless `notify` is false."));
        inventory.function("updateCustom", &UpdateCustom,
            v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("player", "Player"), v8pp::metadata::param("request", "{ id: string; name?: string | null; description?: string | null; data?: Record<string, unknown> | null; revision?: number }")},
                "Updates only named custom fields. Null resets a display override or clears data. Supplied data replaces the old object. The whole row keeps its identity, amount and native properties."));
        inventory.function("setProperties", &SetProperties,
            v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("owner", "Player | Npc"), v8pp::metadata::param("request", "{ id: string; metadata: Record<string, unknown>; revision?: number }")},
                "Replaces a row's properties. The row keeps its identity and amount. It replaces rather than merges: carry over anything you want to keep, and give `health` or `condition`, not two that disagree."));
        inventory.function("set", &Set,
            v8pp::metadata::docs("InventoryResult",
                {v8pp::metadata::param("owner", "Player | Npc"), v8pp::metadata::param("request", "{ items: { id?: string; item: string; amount: number; metadata?: Record<string, unknown>; equipped?: number }[]; quickslots?: InventoryQuickslots | null; revision?: number; harvested?: boolean }")},
                "Replaces an owner's whole inventory, which is how a saved one comes back. It is never announced in the player's game. Rows keep the ids they are given (letters, digits and `-_:.`, up to 64) and get new ones when they have none. `equipped` is how many units of a gear row the body wears, up to 48 in all; their game dresses in them. Optional quickslots restores assignments and selected slots in the current native outfit; every referenced ID must occur in items. Invalid shape or missing references returns invalidQuickslots before changing anything. Omitted or null quickslots leaves assignment to the game's normal equipment handling. Native item eligibility and slot capacity still apply. An empty list clears the inventory."));
        inventory.function("transfer", &Transfer,
            v8pp::metadata::docs("InventoryResult",
                {v8pp::metadata::param("source", "Player | Npc"), v8pp::metadata::param("target", "Player | Npc"), v8pp::metadata::param("request", "{ units: InventoryUnit[]; sourceRevision?: number; targetRevision?: number; notify?: boolean }")},
                "Moves exact units between players or NPCs with their properties, all or nothing. They join matching rows in the target, so the target's row ids are the ones in the result's `items`. The script decides access, distance and consent. Players' games announce what they lost and gained unless `notify` is false."));
        inventory.function("getItemPrice", &GetItemPrice,
            v8pp::metadata::docs("{ pristine: number; current: number } | null", {v8pp::metadata::param("item", "string"), v8pp::metadata::param("metadata", "Record<string, unknown>", true, "The item's properties, as a row's `metadata` holds them.")},
                "What the game prices one unit of an item at, in money units, worked out the way the game does it. `pristine` is the price at the best health its quality allows, `current` at its own health. Quality changes the price only through that health. The metadata reads as `Inventory.add` reads it -- left out, quality 1 at full condition -- so `Inventory.getItemPrice(row.item, row.metadata)` prices a row. This is the item's own worth: what the game's shopkeepers would ask depends on their terms and the haggling, and a `Vendor` charges whatever its script says.",
                "Null for a class the server does not know, or metadata `Inventory.add` would refuse."));
        inventory.function("setItemName", &SetItemName,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("item", "string"), v8pp::metadata::param("name", "string | null")},
                "Changes what every player's game calls an item, in their inventory, the shops, loot and anywhere else the game names it; null gives the game its own name back. Every player sees it, including those who join later, until the server stops.\n\nThe game does not name classes one by one: many classes show the same name -- every kite shield is a Kite Shield -- and renaming one renames all of them. The result lists them. A name is up to 128 bytes with no control characters, and is not translated.",
                "The GUIDs of every class that now shows this name."));
        inventory.function("setItemStats", &SetItemStats,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("item", "string"), v8pp::metadata::param("stats", "{ attack?: number; attackModStab?: number; attackModSlash?: number; attackModSmash?: number; defense?: number; strReq?: number; agiReq?: number; defenseStab?: number; defenseSlash?: number; defenseSmash?: number; noise?: number; power?: number; limbResistance?: number; attackStab?: number; attackSlash?: number; attackSmash?: number; powerMod?: number } | null")},
                "Rewrites the combat numbers of an item class on every player's game, now and for whoever joins later, until the server stops; null gives the class its authored numbers back. The game keeps these on the class, not the item -- every `longsword` shares one row, and the hit calculation reads it on every swing -- so a change here reaches every copy of that class at once and takes effect on the next hit, with nothing to re-equip. A set that names only some stats leaves the others as authored, and replaces the previous set whole.\n\nWhich names a class takes depends on its table element: a `MeleeWeapon` has `attack`, `attackModStab`/`Slash`/`Smash`, `defense`, `strReq`, `agiReq`; `Armor`, `Helmet` and `Hood` have `defenseStab`/`Slash`/`Smash`, `noise`, `strReq`; a `MissileWeapon` has `power`, `limbResistance`, `defense`, `strReq`, `agiReq`; `Ammo` has `attackStab`/`Slash`/`Smash`, `powerMod`. Any other name, a name the element lacks, or a class that is none of these throws. Per-player or per-item numbers are not what this does: for those, put a buff on the body (`player.addBuff`, `horse.addBuff`) with a `weaponAttack`, `weaponDefense` or `attackSpeed` modifier from `Buffs.register`, or register a class of your own with `Items.register`.",
                "True when the numbers were written; false when the class carries none of these stats."));
        inventory.function("getItemStats", &GetItemStats,
            v8pp::metadata::docs("Record<string, number> | null", {v8pp::metadata::param("item", "string")},
                "What this server has rewritten on a class with `setItemStats`, by stat name. The authored numbers are not listed: the server does not carry the game's tables.", "The overridden stats, or null for a class left as authored."));
        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Inventory"), inventory.new_instance()).Check();
        v8pp::module items(isolate, ServerCatalog(), "Items", "Server-defined resource items, drawn with an existing game appearance or with their own model and icon. Register types before restoring their instances.");
        items.function("register", &RegisterItem,
            v8pp::metadata::docs("string",
                {v8pp::metadata::param("id", "string"),
                    v8pp::metadata::param("definition", "{ name: string; description?: string; visual?: string; model?: string; material?: string; icon?: string; weight: number; price?: number; stackable?: boolean; usable?: boolean; useLabel?: string }")},
                "Registers an immutable miscellaneous type and returns its stable GUID. Identical repeats succeed; conflicts throw. Defaults: empty description, price 0, stackable true, usable false, useLabel Use. visual names a miscellaneous item, crafting material or document whose model, material and icon the type borrows; without it, model and icon are required. model is a path relative to Objects/, as the game's item tables write it, which the game prefixes with Objects/ itself (a path starting with objects/ throws) and completes with .cgf when it has no extension: a model the resource streams as stream/objects/kcdc/<resource>/box.cgf is kcdc/<resource>/box.cgf. It draws the item on the ground, in hand and in the inventory preview, and needs a physics proxy for the item to be picked up from the ground: a model without one shows but cannot be picked up. material is a material path in the same form, allowed only with model; without it the model's own material is used. icon is a base64 PNG of at most 256 KiB and 4096x4096 pixels; it is scaled to the stock 64x64 icon size, aspect kept."));
        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Items"), items.new_instance()).Check();
    }
} // namespace KCDC::Scripting
