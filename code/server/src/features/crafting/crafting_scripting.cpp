#include "crafting_scripting.h"
#include "features/smart_object/smart_object_service.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/inventory/inventory_scripting.h"
#include "features/player/player_scripting.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/custom_recipe_registry.h"
#include "shared/game/dlc_catalog.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/scripting/catalog.h"

#include <logging/logger.h>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <optional>
#include <string>

namespace KCDC::Scripting {
    namespace {
        void ReturnJSON(const v8::FunctionCallbackInfo<v8::Value> &info, const nlohmann::json &value) {
            v8::Local<v8::Value> result;
            if (v8::JSON::Parse(info.GetIsolate()->GetCurrentContext(), v8pp::to_v8(info.GetIsolate(), value.dump()).As<v8::String>()).ToLocal(&result)) {
                info.GetReturnValue().Set(result);
            }
        }

        Player *PlayerAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *error) {
            auto *player = info.Length() > index ? v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info[index]) : nullptr;
            if (player == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), error));
            }
            return player;
        }

        std::uint16_t RequiredDLC() {
            std::uint16_t mask = 0;
            for (const std::string &name : Server::Get()->GetModConfig().Get<std::vector<std::string>>("required_dlc")) {
                const auto *dlc = KCDC::Shared::Game::FindDLCByName(name);
                if (dlc != nullptr) {
                    mask |= KCDC::Shared::Game::DLCBit(dlc->id);
                }
            }
            return mask;
        }

        const char *KindName(KCDC::Shared::Game::CraftKind kind) {
            return kind == KCDC::Shared::Game::CraftKind::Alchemy ? "alchemy" : "smithing";
        }

        const char *SourceName(KCDC::Shared::Game::RecipeSource source) {
            switch (source) {
            case KCDC::Shared::Game::RecipeSource::Mod: return "mod";
            case KCDC::Shared::Game::RecipeSource::Script: return "script";
            default: return "game";
            }
        }

        bool ReadKind(const v8::FunctionCallbackInfo<v8::Value> &info, int index, std::optional<KCDC::Shared::Game::CraftKind> &kind) {
            if (info.Length() <= index || info[index]->IsUndefined()) {
                return true;
            }
            const std::string value = info[index]->IsString() ? std::string(v8pp::from_v8<std::string>(info.GetIsolate(), info[index])) : std::string();
            if (value == "alchemy") {
                kind = KCDC::Shared::Game::CraftKind::Alchemy;
            }
            else if (value == "smithing") {
                kind = KCDC::Shared::Game::CraftKind::Smithing;
            }
            else {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting kind must be 'alchemy' or 'smithing'"));
                return false;
            }
            return true;
        }

        void Session(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *player = PlayerAt(info, 0, "Crafting.session requires a Player")) {
                ReturnJSON(info, Server::Get()->GetCrafting().Session(player->GetId()));
            }
        }

        void Occupancy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 1 || info.Length() > 2 || !info[0]->IsString() || (info.Length() == 2 && !info[1]->IsUndefined() && !info[1]->IsUint32())) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting.occupancy requires a station ID and an optional uint32 virtual world"));
                return;
            }
            const auto station      = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            const auto virtualWorld = info.Length() == 2 && !info[1]->IsUndefined() ? v8pp::from_v8<std::uint32_t>(info.GetIsolate(), info[1]) : 0U;
            ReturnJSON(info, Server::Get()->GetCrafting().Occupancy(station, virtualWorld));
        }

        void Cancel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *player = PlayerAt(info, 0, "Crafting.cancel requires a Player")) {
                info.GetReturnValue().Set(Server::Get()->GetCrafting().Cancel(player->GetId()));
            }
        }

        void Knowledge(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *player = PlayerAt(info, 0, "Crafting.knowledge requires a Player")) {
                ReturnJSON(info, Server::Get()->GetCrafting().Knowledge(player->GetId()));
            }
        }

        void SetKnowledge(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *player = PlayerAt(info, 0, "Crafting.setKnowledge requires a Player");
            if (player == nullptr) {
                return;
            }
            // Recipe ids are the decimal strings of Crafting.recipes' key.id.
            std::string recipe;
            if (info.Length() > 1 && info[1]->IsString()) {
                recipe = v8pp::from_v8<std::string>(info.GetIsolate(), info[1]);
            }
            else if (info.Length() > 1 && info[1]->IsUint32()) {
                recipe = std::to_string(v8pp::from_v8<std::uint32_t>(info.GetIsolate(), info[1]));
            }
            if (recipe.empty() || info.Length() != 3 || !info[2]->IsUint32()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting.setKnowledge requires a Player, a recipe id and a uint32 step mask"));
                return;
            }
            info.GetReturnValue().Set(Server::Get()->GetCrafting().SetKnowledge(player->GetId(), recipe, v8pp::from_v8<std::uint32_t>(info.GetIsolate(), info[2])));
        }

        void IsAvailable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::optional<KCDC::Shared::Game::CraftKind> kind;
            if (ReadKind(info, 0, kind)) {
                info.GetReturnValue().Set(true);
            }
        }

        void Recipes(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *player = info.Length() >= 1 ? v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info[0]) : nullptr;
            if (player == nullptr || player->ResolveHuman() == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting.recipes requires a connected Player"));
                return;
            }
            std::optional<KCDC::Shared::Game::CraftKind> kind;
            if (!ReadKind(info, 1, kind)) {
                return;
            }
            nlohmann::json rows          = nlohmann::json::array();
            const std::uint16_t required = RequiredDLC();
            KCDC::Shared::Game::CustomRecipeRegistry::Get().ForEach([&](const KCDC::Shared::Game::CraftRecipe &recipe, KCDC::Shared::Game::RecipeSource source) {
                if ((kind && recipe.kind != *kind) || !KCDC::Shared::Game::CraftDLCEnabled(recipe.dlc, required)) {
                    return;
                }
                nlohmann::json key = {{"kind", KindName(recipe.kind)}};
                if (recipe.kind == KCDC::Shared::Game::CraftKind::Alchemy) {
                    key["id"] = std::stoul(std::string(recipe.id));
                }
                else {
                    key["id"] = recipe.id;
                }
                nlohmann::json ingredients = nlohmann::json::array(), products = nlohmann::json::array();
                for (const auto &material : recipe.ingredients) {
                    const auto *item = KCDC::Shared::Game::FindItemClass(material.item);
                    const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(material.item);
                    ingredients.push_back({{"item", material.item}, {"name", item != nullptr ? item->name : material.item}, {"amount", material.amount}, {"quest", properties != nullptr && properties->questItem}});
                }
                for (const auto &product : recipe.products) {
                    products.push_back({{"item", product.item}, {"threshold", product.threshold}, {"maxYield", product.maxYield}});
                }
                rows.push_back({{"key", key}, {"name", recipe.name}, {"dlcId", recipe.dlc}, {"minSkill", recipe.minSkill}, {"perk", recipe.perk}, {"base", recipe.base}, {"ingredients", ingredients}, {"products", products}, {"source", SourceName(source)}});
            });
            ReturnJSON(info, rows);
        }

        void RegisterRecipe(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsObject()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting.registerRecipe requires a recipe definition"));
                return;
            }
            KCDC::Shared::Game::CustomRecipeDefinition definition;
            std::string error;
            std::shared_ptr<const KCDC::Shared::Game::CustomRecipe> recipe;
            if (KCDC::Shared::Game::CustomRecipeFromJson(InventoryRequestArgument(info, 0), definition, error)) {
                recipe = KCDC::Shared::Game::CustomRecipe::Compile(std::move(definition), error);
            }
            if (recipe == nullptr || !KCDC::Shared::Game::CustomRecipeRegistry::Get().Register(recipe, error)) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Crafting.registerRecipe: " + error));
                return;
            }
            Server::Get()->GetCrafting().PublishRecipes();
        }

        void Stations(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::optional<KCDC::Shared::Game::CraftKind> kind;
            if (!ReadKind(info, 0, kind)) {
                return;
            }
            const MafiaNet::VirtualWorldId virtualWorld = info.Length() > 1 && info[1]->IsNumber() ? static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[1])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;
            nlohmann::json rows                         = nlohmann::json::array();
            for (const KCDC::Shared::Game::CraftStation *station : KCDC::Features::SmartObject::SmartObjectService::Get().CraftStations(virtualWorld)) {
                if (kind && station->kind != *kind) {
                    continue;
                }
                rows.push_back({{"id", station->id}, {"kind", KindName(station->kind)}, {"level", station->level}, {"layer", station->layer}, {"position", {{"x", station->x}, {"y", station->y}, {"z", station->z}}},
                    {"forward", {{"x", station->forwardX}, {"y", station->forwardY}}}, {"placed", KCDC::Shared::Game::IsSmartObjectGuid(station->guid)}});
            }
            ReturnJSON(info, rows);
        }

        bool Review(const char *eventName, std::uint64_t playerId, const nlohmann::json &proposal) {
            return EmitReservedEventSync(eventName, [playerId, &proposal](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), FrozenValue(isolate, proposal)};
            });
        }
    } // namespace

    bool CraftingStarting(std::uint64_t playerId, const nlohmann::json &proposal) {
        return Review("craftingStarting", playerId, proposal);
    }

    bool CraftingCompleting(std::uint64_t playerId, const nlohmann::json &proposal) {
        return Review("craftingCompleting", playerId, proposal);
    }

    bool CraftingRefunding(std::uint64_t playerId, const nlohmann::json &proposal) {
        bool requested = false;
        WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            v8::MicrotasksScope microtasks(context, v8::MicrotasksScope::kDoNotRunMicrotasks);
            // V8 owns this state, so retaining the callback cannot retain a
            // stack pointer. Close it before returning to material settlement.
            auto state = v8::Array::New(isolate, 2);
            state->Set(context, 0, v8::True(isolate)).Check();
            state->Set(context, 1, v8::False(isolate)).Check();
            auto refund = v8::Function::New(
                context,
                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto state   = info.Data().As<v8::Array>();
                    auto context = info.GetIsolate()->GetCurrentContext();
                    if (state->Get(context, 0).ToLocalChecked()->IsTrue()) {
                        state->Set(context, 1, v8::True(info.GetIsolate())).Check();
                    }
                },
                state)
                              .ToLocalChecked();
            const std::vector<v8::Local<v8::Value>> args {WrapPlayer(isolate, playerId), FrozenValue(isolate, proposal), refund};
            // Handler return values are ignored: refund() is the only request.
            resources->GetEvents().EmitReservedSync(isolate, context, "craftingRefunding", args);
            state->Set(context, 0, v8::False(isolate)).Check();
            requested = state->Get(context, 1).ToLocalChecked()->IsTrue();
        });
        return requested;
    }

    void EmitCraftingEvent(const std::string &name, std::uint64_t playerId, const nlohmann::json &payload) {
        EmitReservedEvent(name.c_str(), [playerId, &payload](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), FrozenValue(isolate, payload)};
        });
    }

    void RegisterCraftingBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &starting = ServerCatalog().data_type("CraftStartProposal", "A table a player is about to open, or a workpiece about to take its materials. Frozen: a handler can refuse it, not change it.");
        starting.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        starting.add_property("station", "string", "Station id, as `Crafting.stations` names it.");
        starting.add_property("virtualWorld", "number", "The player's virtual world; each world has its own stations.");
        starting.add_property("recipe", "string | undefined", "Smithing only: the recipe id the player chose, as `Crafting.recipes` keys it.");
        starting.add_property("continuationOf", "string", "The batch this one follows at a table the player kept, or empty for a fresh entry. Always empty for smithing.");

        auto &started = ServerCatalog().data_type("CraftStartedEvent", "A batch that is now brewing, or a workpiece whose materials were taken.");
        started.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        started.add_property("session", "string", "The batch's or workpiece's session id.");
        started.add_property("station", "string", "Station id.");
        started.add_property("virtualWorld", "number", "The station's virtual world.");
        started.add_property("recipe", "string | undefined", "Smithing only: the recipe being worked.");
        started.add_property("continuationOf", "string", "The previous batch at this table, or empty for the first. Always empty for smithing.");

        auto &completing = ServerCatalog().data_type("CraftCompletionProposal", "What a finished batch or workpiece is about to grant, computed by the server. Frozen: a handler can refuse it, not change it.");
        completing.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        completing.add_property("session", "string", "The session id.");
        completing.add_property("station", "string", "Station id.");
        completing.add_property("outcome", "'success' | 'failed'", "`failed` brews the game's failed potion. A smithing proposal is always `success`: a failed workpiece grants nothing to review.");
        completing.add_property("recipe", "string", "The recipe the brew matched, or empty when it matched none; the smithing recipe worked.");
        completing.add_property("grade", "number", "The product's native rank; for smithing, its item quality tier.");
        completing.add_property("product", "string", "Item class GUID of what would be granted, or empty when nothing is: a zero alchemy yield, or a smithing quest product its quest creates.");
        completing.add_property("amount", "number", "How many.");
        completing.add_property("quality", "number", "Brewing quality, 0 to 1, after perks and the table-entry bonus; for smithing, the workpiece quality the player's game reported.");
        completing.add_property("xp", "number", "Base alchemy or craftsmanship XP; the player's own multipliers apply on top.");
        completing.add_property("lockpicks", "number | undefined", "Smithing only: lockpicks granted alongside, drawn by the server from the player's perks.");

        auto &completed = ServerCatalog().data_type("CraftCompletedEvent", "A batch or workpiece whose result was granted: the output is in the inventory and the XP was ordered.");
        completed.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        completed.add_property("session", "string", "The session id.");
        completed.add_property("station", "string", "Station id.");
        completed.add_property("virtualWorld", "number", "The station's virtual world.");
        completed.add_property("outcome", "'success' | 'failed'", "`failed` granted the game's failed potion.");
        completed.add_property("recipe", "string", "The recipe matched, or empty.");
        completed.add_property("grade", "number", "The product's native rank; for smithing, its item quality tier.");
        completed.add_property("quality", "number", "Brewing or workpiece quality, 0 to 1.");
        completed.add_property("xp", "number", "Base XP ordered.");
        completed.add_property("lockpicks", "number | undefined", "Smithing only: lockpicks granted.");
        completed.add_property("outputs", "InventoryUnit[]", "The inventory rows that received the output, lockpicks included. Do not grant it again.");
        completed.add_property("knowledge", "Record<string, number> | undefined", "Alchemy only: the player's recipe knowledge after this batch, as `Crafting.knowledge` returns it.");

        auto &refundMaterial = ServerCatalog().data_type("CraftRefundMaterial", "An original ingredient still held by this session, including its original inventory properties.");
        refundMaterial.add_property("item", "string", "Item class GUID.");
        refundMaterial.add_property("amount", "number", "Units originally removed and not already returned.");
        refundMaterial.add_property("metadata", "Record<string, unknown>", "Original canonical inventory properties, preserved by refunds.");
        auto &refunding = ServerCatalog().data_type("CraftRefundProposal", "An unfinished craft before material settlement. Frozen. Call the event's refund callback synchronously to request all its materials back; otherwise the native refund rules apply.");
        refunding.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        refunding.add_property("session", "string", "The session being settled, once only.");
        refunding.add_property("station", "string", "Station id.");
        refunding.add_property("virtualWorld", "number", "The session's virtual world.");
        refunding.add_property("outcome", "'failed' | 'cancelled'", "No product was granted for this craft.");
        refunding.add_property("reason", "string",
            "Why it ended: cancelled for normal native or script cancellation; interrupted for native teardown without a normal exit; clientError for an alchemy adapter failure; craftingFailed for a broken smithing workpiece. Server closures also include disconnected, timeout and contextInvalidated; settlement refusals retain their existing reasons. Client exit reasons are observations, not server-verified combat evidence.");
        refunding.add_property("materials", "CraftRefundMaterial[]", "All remaining original ingredients, including processed alchemy ingredients. Station liquids and ingredients already returned are excluded. Do not grant these yourself; call refund().");

        auto &ended = ServerCatalog().data_type("CraftEndedEvent", "A batch or workpiece that is over, for any reason. A table kept for the next batch is not closed by this.");
        ended.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        ended.add_property("session", "string", "The session id.");
        ended.add_property("station", "string", "Station id.");
        ended.add_property("virtualWorld", "number", "The station's virtual world.");
        ended.add_property("outcome", "'success' | 'failed' | 'cancelled'", "How it ended.");
        ended.add_property("reason", "string",
            "Empty for a completed batch or a successful workpiece. `craftingFailed` for a broken workpiece; `cancelled` for normal native or script cancellation; `interrupted` for unexpected native teardown; `clientError` for alchemy adapter errors. Other reasons: "
            "`craftingCompletingRejected`, `disconnected`, `timeout`, `contextInvalidated`, and for smithing `abandoned`, `abandonTooLate`, `invalidQuality`, `inventoryUnavailable`. See CraftRefundProposal.");
        ended.add_property("refunded", "InventoryUnit[]",
            "The inventory rows ingredients went back to. Alchemy: wholly unmilled bowl, mortar and herb groups come back; everything else put on the table was spent. Smithing: a failed workpiece spends half of each divisible material, rounded down, and a coin decides a single "
            "unit; the rest comes back. A full refund requested through craftingRefunding returns all remaining original materials instead.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("craftingRefunding", "[player: Player, proposal: CraftRefundProposal, refund: () => void]",
            "Synchronous decision before an unfinished craft settles, including when no native refund is due. Call refund() to request a full refund; return values are ignored. No handler or no call preserves native rules. JavaScript exceptions are logged and do not cancel a call already made. The callback expires on return; delayed calls do nothing. "
            "The `cancelled`, `interrupted` and `clientError` reasons are reported by the player's own client: a modified client can send any of them, so refunding on one lets that client keep its materials. Never grants a completed craft twice. craftingEnded follows the actual inventory changes.");
        events.add_property("craftingStarting", "[player: Player, proposal: CraftStartProposal]", "A player asks to open an alchemy table, or to begin a smithing recipe. Every handler runs; one returning literal `false` refuses it. An async handler cannot refuse.");
        events.add_property("craftingStarted", "[player: Player, event: CraftStartedEvent]", "A batch is brewing, or a workpiece took its materials.");
        events.add_property("craftingCompleting", "[player: Player, proposal: CraftCompletionProposal]",
            "A batch or workpiece finished and its result is computed. Every handler runs; one returning literal `false` refuses it: alchemy ends the batch as failed, what was spent stays spent, and nothing is granted; smithing fails the workpiece, spending its failure share.");
        events.add_property("craftingCompleted", "[player: Player, event: CraftCompletedEvent]", "A result was granted, followed by `craftingEnded`. Raised after the `playerInventoryChanged` it caused.");
        events.add_property("craftingEnded", "[player: Player, event: CraftEndedEvent]", "A batch or workpiece is over. Raised after the `playerInventoryChanged` of any refund.");

        auto &material = ServerCatalog().data_type("CraftMaterialRequirement", "Units required by a shipped recipe.");
        material.add_property("item", "string", "Item class GUID.");
        material.add_property("name", "string", "Native item name, useful with Player.giveItem.");
        material.add_property("amount", "number", "Units required for one attempt.");
        material.add_property("quest", "boolean", "A quest item, which only its quest hands out: the game cannot create one, so the inventory refuses it (`questItem`) and Player.giveItem returns false. It must be held to craft, and is never taken.");
        auto &product = ServerCatalog().data_type("CraftProductInfo", "One possible product row. These are alternatives, not a reward list.");
        product.add_property("item", "string", "Product class GUID.");
        product.add_property("threshold", "number", "Alchemy product rank or smithing minimum quality percent.");
        product.add_property("maxYield", "number", "Alchemy base maximum yield before modifiers; one for smithing.");
        auto &recipe = ServerCatalog().data_type("CraftRecipeInfo", "A recipe allowed by server DLC policy: the game's, a streamed mod's or a script's. Presence is not proof of player knowledge, skill, quest eligibility or materials.");
        recipe.add_property("key", "{ kind: 'alchemy'; id: number } | { kind: 'smithing'; id: string }", "Tagged native recipe identity.");
        recipe.add_property("name", "string", "Localization key from the native table.");
        recipe.add_property("dlcId", "number", "Native DLC id; zero for untagged content.");
        recipe.add_property("minSkill", "number", "Minimum craftsmanship skill, or zero for alchemy.");
        recipe.add_property("perk", "string", "Smithing recipe unlock perk GUID, empty for alchemy.");
        recipe.add_property("base", "string", "Alchemy base liquid, empty for smithing. Supplied by the station.");
        recipe.add_property("ingredients", "CraftMaterialRequirement[]", "Required ingredient quantities.");
        recipe.add_property("products", "CraftProductInfo[]", "Possible products by native rank or threshold.");
        recipe.add_property("source", "'game' | 'mod' | 'script'", "Where the recipe comes from: the game's tables, a streamed mod's table patch (which may replace a game recipe of the same id), or `Crafting.registerRecipe`.");

        auto &step = ServerCatalog().data_type("CraftRecipeStepDefinition", "One step of a custom alchemy recipe, as a row of the game's `AlchemyRecipe.xml` has it.");
        step.add_property("id", "number", "1 to 28, distinct within the recipe: the step's bit in the knowledge mask. The first step listed identifies the recipe: a brew that fails it is not this recipe.");
        step.add_property("type", "'Trivial' | 'Easy' | 'Hard'", "The step's XP: 0, 12 or 24, from `AlchemyRecipeStepType.xml`.");
        step.add_property("condition", "string",
            "The game's own condition syntax, terms joined by ` AND `: `PotionBase()`, `IngredientCount('<item>') == n` or `>= n`, `IsMilled('<item>', 0 | 1)`, `WeakBoilingTime('<item>', time)`, `StrongBoilingTime('<item>', time)`, `IsBaseDistilled(0 | 1)` and `IsResultMilled(0 | 1)`. Item GUIDs in lower case; a time is in the table's time units. Anything else is refused.");
        step.add_property("feedbackPriority", "number | undefined", "The order the recipe book lists failed steps in. Default 0.");
        step.add_property("text", "string | undefined", "The step's line in the recipe book, shown as written. Default none.");

        auto &definition = ServerCatalog().data_type("CraftRecipeDefinition",
            "A recipe for `Crafting.registerRecipe`, field for field a row of the game's `AlchemyRecipe.xml` or `BlacksmithRecipes.xml`. Items are item class GUIDs of the game or of a streamed mod; custom items from `Items.register` cannot be used. Texts are shown as written. Pictures are base64 PNGs, each at most 4096x4096 pixels, 256 KiB of PNG for the recipe's pictures together.");
        definition.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        definition.add_property("id", "number | string",
            "Alchemy: a RecipeId from 1 to 2147483647 the game does not use; when two recipes would identify the same brew, the lower id wins, as in the game. Smithing: an Id of letters, digits, `_`, `-` and `.`, at most 128 characters, the game does not use.");
        definition.add_property("name", "string", "The recipe's name in the recipe book or the smithy.");
        definition.add_property("icon", "string | undefined", "UIIcon: one of the game's item icons, by name, such as `potion_hair_o_dog` or `shortswordCommon`. Leave it out when `iconImage` brings one.");
        definition.add_property("iconImage", "string | undefined", "The recipe's own icon in the recipe lists, a base64 PNG. Fitted onto the game's 64x64 item icon square, aspect kept, so a square image with the item reaching the edges looks like the game's.");
        definition.add_property("productImage", "string | undefined", "Alchemy: the recipe book's picture of the product, beside the description, a base64 PNG. Fitted onto a 256x256 square, as the game's are.");
        definition.add_property("recipeImage", "string | undefined", "Alchemy: the recipe book's recipe picture, a base64 PNG. Fitted onto a 1024x1024 square, as the game's are.");
        definition.add_property("bookPicturesFrom", "number | undefined",
            "Alchemy: the RecipeId of one of the game's alchemy recipes whose two book pictures the recipe book shows for this one, instead of productImage and recipeImage. `Crafting.recipes(player, 'alchemy')` lists the ids (`key.id`).");
        definition.add_property("dlcId", "number | undefined", "Native DLC id the recipe belongs to, filtered by `required_dlc` as the game's are. Default 0.");
        definition.add_property("minSkill", "number | undefined", "Smithing: the craftsmanship it takes. Default 0.");
        definition.add_property("ingredients", "{ item: string; amount: number }[]", "1 to 64 distinct items and the units one attempt takes. A quest item must be held but is never taken.");
        definition.add_property("products", "{ item: string; threshold: number; maxYield?: number }[]",
            "1 to 64 products, each with its own threshold. Alchemy: threshold is the potion grade, 1 to 4, and maxYield the potions it makes, 1 to 64; the potion is an item the inventory holds, not a quest item. Smithing: threshold is the minimal workpiece quality percent, 0 to 100; the product is a melee weapon, armor that never changes condition, or a misc item, and not a quest item.");
        definition.add_property("base", "'Water' | 'Wine' | 'Oil' | 'Spiritus' | undefined", "Alchemy, required: the base liquid.");
        definition.add_property("usage", "string | undefined", "Alchemy: the recipe book's usage text.");
        definition.add_property("baseXP", "number | undefined", "Alchemy: BaseQualityRewardXP, the brewing XP before grade multipliers and step XP. Default 0.");
        definition.add_property("steps", "CraftRecipeStepDefinition[] | undefined", "Alchemy, required: 1 to 28 steps.");
        definition.add_property("perk", "string | undefined", "Smithing: the perk GUID that teaches the recipe. Without one, every smith knows it.");
        definition.add_property("category", "'ShortSword' | 'LongSword' | 'Axe' | 'Other' | undefined", "Smithing, required: the recipe screen's tab.");
        definition.add_property("workpiece", "'sword' | 'longsword' | 'axe' | 'horseshoe' | undefined", "Smithing, required: the blank on the anvil.");
        definition.add_property("description", "string | undefined", "Smithing: the description the smithy shows.");
        auto &station = ServerCatalog().data_type("CraftStationInfo",
            "A station in the configured level: one the level authors, or one a script or a World Builder export placed. The catalog does not guarantee that a level station's quest layer is loaded or that native use is currently permitted.");
        station.add_property("id", "string", "`<level>:<guid>`. A level station's GUID is stable; a placed one's lasts until the server stops.");
        station.add_property("placed", "boolean", "Whether a script or a World Builder export placed the station, rather than the level. Its `layer` is empty.");
        station.add_property("kind", "'alchemy' | 'smithing'", "The station's recipe family.");
        station.add_property("level", "string", "Level directory name.");
        station.add_property("layer", "string", "Authored editor layer, for identifying the location.");
        station.add_property("position", "{ x: number; y: number; z: number }", "World position of the station.");
        station.add_property("forward", "{ x: number; y: number }", "Horizontal facing direction.");

        auto &occupant = ServerCatalog().data_type("CraftStationOccupant", "Who holds a station. A player keeps an alchemy table between batches until they leave it; a smithery is held for one workpiece.");
        occupant.add_property("playerId", "number", "The player's id.");
        occupant.add_property("entryId", "string", "The first batch of this visit; the same across every batch of it.");
        occupant.add_property("sessionId", "string", "The batch now running, or the last one to finish.");
        occupant.add_property("phase", "'entering' | 'brewing' | 'finishing' | 'betweenBatches' | 'working'", "`entering` until the opening animation finishes; `finishing` while a finished batch waits to settle; `working` at a smithery.");
        occupant.add_property("expiresInMs", "number", "Milliseconds until the table is taken back. Each batch has 30 minutes.");
        auto &occupancy = ServerCatalog().data_type("CraftStationOccupancy", "One station in one virtual world. A free station may still be unusable in the game, for instance behind a quest layer.");
        occupancy.add_property("station", "string", "Station id.");
        occupancy.add_property("virtualWorld", "number", "The virtual world asked about.");
        occupancy.add_property("occupied", "boolean", "Someone holds it, brewing or between batches.");
        occupancy.add_property("occupant", "CraftStationOccupant | null", "Who, or null when free.");

        auto &session = ServerCatalog().data_type("CraftSessionInfo", "A player's alchemy session or kept table, or their smithing workpiece. A copy: changing it changes nothing.");
        session.add_property("kind", "'alchemy' | 'smithing'", "Which craft.");
        session.add_property("id", "string", "The session id.");
        session.add_property("station", "string", "Station id.");
        session.add_property("virtualWorld", "number", "The station's virtual world.");
        session.add_property("phase", "'entering' | 'brewing' | 'finishing' | 'betweenBatches' | 'working'", "Where the batch is; `working` for a smithing workpiece.");
        session.add_property("sequence", "number | undefined", "Alchemy only: native actions accepted so far.");
        session.add_property("resources", "{ id: number; item: string; itemId: string; position: number; base: boolean; milled: boolean; distilled: boolean }[] | undefined",
            "Alchemy only: what is on the table: item class, the inventory row it came from (empty for a base liquid), and the native table position.");
        session.add_property("recipe", "string | undefined", "Smithing only: the recipe being worked.");
        session.add_property("materials", "{ item: string; amount: number }[] | undefined", "Smithing only: the units taken from the inventory for this workpiece. Quest materials are required but never taken.");
        session.add_property("expiresInMs", "number", "Milliseconds until the session times out.");

        v8pp::module crafting(isolate, ServerCatalog(), "Crafting", "Crafting catalogs, station occupancy, alchemy and smithing sessions, and alchemy recipe knowledge. Held in memory for each connection; nothing is saved.");
        crafting.function("isAvailable", &IsAvailable, v8pp::metadata::docs("boolean", {v8pp::metadata::param("kind", "'alchemy' | 'smithing'", true)}, "True: alchemy and smithing are both ruled by the server. Omitted kind checks alchemy."));
        crafting.function("stations", &Stations,
            v8pp::metadata::docs("readonly CraftStationInfo[]", {v8pp::metadata::param("kind", "'alchemy' | 'smithing'", true), v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world whose stations to list; the global one when omitted.")},
                "The stations players in a virtual world can craft at: the level's own, minus any a level edit took out of that world, and every station `SmartObject.spawn` or a World Builder export placed there or in the global world. Does not load deferred layers or bypass quests."));
        crafting.function("recipes", &Recipes,
            v8pp::metadata::docs("readonly CraftRecipeInfo[]", {v8pp::metadata::param("player", "Player"), v8pp::metadata::param("kind", "'alchemy' | 'smithing'", true)},
                "Every recipe enabled by server DLC policy: the game's, then streamed mods' and scripts' in the order they were added. A mod's table patch row with a game recipe's id replaces it in place. The player must be connected but does not change the list. Catalog presence does not establish knowledge or skill."));
        crafting.function("registerRecipe", &RegisterRecipe,
            v8pp::metadata::docs("void", {v8pp::metadata::param("definition", "CraftRecipeDefinition")},
                "Adds an alchemy or smithing recipe for the rest of the server's life. The server brews and forges it by the same rules as the game's recipes, `Crafting.recipes` lists it, and every client, including one that joins later, adds it to the game's own recipe book and smithy. Registering the same definition again does nothing. "
                "Throws for a malformed definition, an item the catalog does not know, an id the game or another resource uses, or a different definition under an id already registered."));
        crafting.function("session", &Session, v8pp::metadata::docs("CraftSessionInfo | null", {v8pp::metadata::param("player", "Player")}, "The player's alchemy session or the table they kept between batches, else their smithing workpiece, else null."));
        crafting.function("occupancy", &Occupancy,
            v8pp::metadata::docs("CraftStationOccupancy | null", {v8pp::metadata::param("stationId", "string"), v8pp::metadata::param("virtualWorld", "number", true)}, "Who holds a station. World defaults to 0. Null for an unknown station."));
        crafting.function("cancel", &Cancel,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player")},
                "Ends the player's session as leaving the station would, and their game closes it. Alchemy: wholly unmilled groups go back to the inventory, the rest is spent. Smithing: the workpiece fails, spending its failure share. `craftingEnded` follows next tick. False when there was nothing to end."));
        crafting.function("knowledge", &Knowledge,
            v8pp::metadata::docs("Record<string, number>", {v8pp::metadata::param("player", "Player")},
                "The player's recipe knowledge: recipe id to a mask of its 29 authored steps, 536870911 when the whole recipe is known. A recipe not listed is unknown. Starts empty every session; save it from `craftingCompleted` or here and restore it with `setKnowledge`."));
        crafting.function("setKnowledge", &SetKnowledge,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player"), v8pp::metadata::param("recipeId", "string | number"), v8pp::metadata::param("mask", "number")},
                "Sets which steps of one recipe the player knows, and sends it to their game's recipe book. 0 forgets the recipe. False for an unknown recipe, a mask above 536870911, or a player who is not connected."));
        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Crafting"), crafting.new_instance()).Check();
    }
} // namespace KCDC::Scripting
