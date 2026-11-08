#include "features/vendor/vendor_scripting.h"
#include "features/inventory/inventory_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "features/player/player_roster.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** A whole number in [minimum, maximum], or nullopt for anything else. */
        std::optional<std::uint32_t> WholeNumber(v8::Isolate *isolate, v8::Local<v8::Value> value, std::uint32_t minimum, std::uint32_t maximum) {
            if (value.IsEmpty() || !value->IsNumber()) {
                return std::nullopt;
            }
            const double number = v8pp::from_v8<double>(isolate, value);
            if (!std::isfinite(number) || number != std::floor(number) || number < minimum || number > maximum) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(number);
        }

        /** The item a row names, by class GUID or by the game's own item name. */
        const Shared::Game::ItemClassEntry *RowItem(v8::Isolate *isolate, v8::Local<v8::Object> row) {
            const v8::Local<v8::Value> item = OptionField(isolate, row, "item");
            if (item.IsEmpty() || !item->IsString()) {
                return nullptr;
            }
            return Features::Player::Players::ResolveItemClass(v8pp::from_v8<std::string>(isolate, item));
        }

        /**
         * Walks a script array of rows, handing each object to `read`.
         *
         * The limit is the wire's: a list is sent whole every time it changes,
         * so a list that does not fit in one message is refused here rather
         * than truncated there.
         */
        template <typename Read>
        bool ReadRows(v8::Isolate *isolate, v8::Local<v8::Value> value, std::string &error, Read &&read) {
            if (value.IsEmpty() || !value->IsArray()) {
                error = "expected an array of rows";
                return false;
            }
            v8::Local<v8::Array> array     = value.As<v8::Array>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (array->Length() > Shared::RPC::kMaxVendorRows) {
                error = "a list carries at most " + std::to_string(Shared::RPC::kMaxVendorRows) + " rows";
                return false;
            }
            for (std::uint32_t index = 0; index < array->Length(); ++index) {
                v8::Local<v8::Value> entry;
                if (!array->Get(context, index).ToLocal(&entry) || !entry->IsObject()) {
                    error = "every row must be an object";
                    return false;
                }
                if (!read(entry.As<v8::Object>(), index)) {
                    return false;
                }
            }
            return true;
        }

        std::string RowError(std::uint32_t index, const char *what) {
            return "row " + std::to_string(index) + ": " + what;
        }

        std::uint32_t VendorArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsNumber() ? v8pp::from_v8<std::uint32_t>(info.GetIsolate(), info[index]) : 0;
        }

        void ThrowType(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, message)));
        }

        // JS: Vendor.create(options?) -> number
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            std::string name;
            std::optional<std::uint32_t> purse;
            bool buys = true;

            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Object> options    = info[0].As<v8::Object>();
                const v8::Local<v8::Value> label = OptionField(isolate, options, "name");
                if (!label.IsEmpty() && label->IsString()) {
                    name = v8pp::from_v8<std::string>(isolate, label);
                }
                const v8::Local<v8::Value> money = OptionField(isolate, options, "purse");
                if (!money.IsEmpty() && !money->IsUndefined() && !money->IsNull()) {
                    purse = WholeNumber(isolate, money, 0, std::numeric_limits<std::uint32_t>::max());
                    if (!purse.has_value()) {
                        ThrowType(isolate, "Vendor.create: purse must be a whole number of money units, or omitted for a purse that never runs out");
                        return;
                    }
                }
                const v8::Local<v8::Value> buying = OptionField(isolate, options, "buys");
                if (!buying.IsEmpty() && buying->IsBoolean()) {
                    buys = v8pp::from_v8<bool>(isolate, buying);
                }
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, Features::Vendor::VendorService::Get().Create(std::move(name), purse, buys)));
        }

        // JS: Vendor.setStock(vendor, rows) -> boolean
        void JS_SetStock(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            std::vector<Features::Vendor::StockRow> stock;
            std::string error;
            const bool read = ReadRows(isolate, info.Length() >= 2 ? info[1] : v8::Local<v8::Value> {}, error, [&](v8::Local<v8::Object> row, std::uint32_t index) {
                Features::Vendor::StockRow parsed;
                parsed.item                               = RowItem(isolate, row);
                const std::optional<std::uint32_t> amount = WholeNumber(isolate, OptionField(isolate, row, "amount"), 1, Shared::RPC::kMaxInventoryStackAmount);
                const std::optional<std::uint32_t> price  = WholeNumber(isolate, OptionField(isolate, row, "price"), 0, Shared::RPC::kMaxVendorPrice);
                if (parsed.item == nullptr) {
                    error = RowError(index, "item is not a known item class GUID or name");
                    return false;
                }
                if (!amount.has_value() || !price.has_value()) {
                    error = RowError(index, "amount must be a whole number from 1, and price a whole number of money units from 0");
                    return false;
                }
                // One row per class: the trade screen prices by class, so a
                // second row would be a second price nothing could show.
                if (std::any_of(stock.begin(), stock.end(), [&parsed](const Features::Vendor::StockRow &existing) {
                        return existing.item == parsed.item;
                    })) {
                    error = RowError(index, "lists the same item as an earlier row");
                    return false;
                }
                const auto metadata = OptionField(isolate, row, "metadata");
                if (!metadata.IsEmpty() && !metadata->IsUndefined()) {
                    v8::Local<v8::String> encoded;
                    if (!metadata->IsObject() || !v8::JSON::Stringify(isolate->GetCurrentContext(), metadata).ToLocal(&encoded)) {
                        error = "metadata must be an object";
                        return false;
                    }
                    const auto text = v8pp::from_v8<std::string>(isolate, encoded);
                    if (text.size() > 32768) {
                        error = "metadata is too large";
                        return false;
                    }
                    parsed.metadata = nlohmann::json::parse(text, nullptr, false);
                }
                parsed.amount = *amount;
                parsed.price  = *price;
                stock.push_back(parsed);
                return true;
            });
            if (!read) {
                ThrowType(isolate, "Vendor.setStock: " + error);
                return;
            }
            info.GetReturnValue().Set(Features::Vendor::VendorService::Get().SetStock(VendorArg(info, 0), std::move(stock)));
        }

        // JS: Vendor.setBuyPrices(vendor, rows) -> boolean
        void JS_SetBuyPrices(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            std::vector<Features::Vendor::BuyRow> buys;
            std::string error;
            const bool read = ReadRows(isolate, info.Length() >= 2 ? info[1] : v8::Local<v8::Value> {}, error, [&](v8::Local<v8::Object> row, std::uint32_t index) {
                Features::Vendor::BuyRow parsed;
                parsed.item                              = RowItem(isolate, row);
                const std::optional<std::uint32_t> price = WholeNumber(isolate, OptionField(isolate, row, "price"), 0, Shared::RPC::kMaxVendorPrice);
                if (parsed.item == nullptr) {
                    error = RowError(index, "item is not a known item class GUID or name");
                    return false;
                }
                if (!price.has_value()) {
                    error = RowError(index, "price must be a whole number of money units from 0");
                    return false;
                }
                if (std::any_of(buys.begin(), buys.end(), [&parsed](const Features::Vendor::BuyRow &existing) {
                        return existing.item == parsed.item;
                    })) {
                    error = RowError(index, "lists the same item as an earlier row");
                    return false;
                }
                parsed.price = *price;
                buys.push_back(parsed);
                return true;
            });
            if (!read) {
                ThrowType(isolate, "Vendor.setBuyPrices: " + error);
                return;
            }
            info.GetReturnValue().Set(Features::Vendor::VendorService::Get().SetBuys(VendorArg(info, 0), std::move(buys)));
        }

        // JS: Vendor.setPurse(vendor, purse) -> boolean
        void JS_SetPurse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            std::optional<std::uint32_t> purse;
            if (info.Length() >= 2 && !info[1]->IsNull() && !info[1]->IsUndefined()) {
                purse = WholeNumber(isolate, info[1], 0, std::numeric_limits<std::uint32_t>::max());
                if (!purse.has_value()) {
                    ThrowType(isolate, "Vendor.setPurse: purse must be a whole number of money units, or null for a purse that never runs out");
                    return;
                }
            }
            info.GetReturnValue().Set(Features::Vendor::VendorService::Get().SetPurse(VendorArg(info, 0), purse));
        }

        // JS: Vendor.getPurse(vendor) -> number | null
        void JS_GetPurse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                     = info.GetIsolate();
            const std::optional<std::uint32_t> purse = Features::Vendor::VendorService::Get().GetPurse(VendorArg(info, 0));
            if (!purse.has_value()) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, *purse));
        }

        // JS: Vendor.open(vendor, player, npc?) -> number
        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 2 || !info[0]->IsNumber() || !info[1]->IsNumber()) {
                ThrowType(isolate, "Vendor.open takes a vendor id, a player id and optionally an NPC id");
                return;
            }
            const std::uint64_t playerId = v8pp::from_v8<std::uint64_t>(isolate, info[1]);
            const std::uint64_t npcId    = info.Length() >= 3 && info[2]->IsNumber() ? v8pp::from_v8<std::uint64_t>(isolate, info[2]) : 0;
            info.GetReturnValue().Set(v8pp::to_v8(isolate, Features::Vendor::VendorService::Get().Open(VendorArg(info, 0), playerId, npcId)));
        }

        bool Destroy(std::uint32_t vendorId) {
            return Features::Vendor::VendorService::Get().Destroy(vendorId);
        }

        bool Close(std::uint32_t sessionId) {
            return Features::Vendor::VendorService::Get().Close(sessionId, Shared::RPC::VendorCloseReason::Server);
        }

        std::uint32_t SessionOf(std::uint64_t playerId) {
            return Features::Vendor::VendorService::Get().SessionOf(playerId);
        }

        std::uint32_t VendorOf(std::uint32_t sessionId) {
            return Features::Vendor::VendorService::Get().VendorOf(sessionId);
        }

        v8::Local<v8::Array> TradeLines(v8::Isolate *isolate, const std::vector<Features::Vendor::TradeLine> &lines) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> array     = v8::Array::New(isolate, static_cast<int>(lines.size()));
            for (std::uint32_t index = 0; index < lines.size(); ++index) {
                const Features::Vendor::TradeLine &line = lines[index];
                v8::Local<v8::Object> row               = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "item"), v8pp::to_v8(isolate, std::string(line.item->guid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(line.item->name))).Check();
                row->Set(context, v8pp::to_v8(isolate, "amount"), v8pp::to_v8(isolate, line.amount)).Check();
                row->Set(context, v8pp::to_v8(isolate, "price"), v8pp::to_v8(isolate, line.price)).Check();
                row->Set(context, v8pp::to_v8(isolate, "metadata"), FrozenValue(isolate, line.metadata)).Check();
                array->Set(context, index, row).Check();
            }
            return array;
        }
    } // namespace

    void RegisterVendorBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        // Declared types, not inline shapes: the contract only carries named
        // ones. None is called `Vendor` -- that name is the global module, and
        // a catalog name used for both kills the server at boot.
        auto &options = ServerCatalog().data_type("VendorOptions", "How a vendor starts out.");
        options.add_property("name", v8pp::metadata::type{"string | undefined", {}, true}, "Shown in the server's log only. The trade screen names whoever keeps the shop.");
        options.add_property("purse", v8pp::metadata::type{"number | undefined", {}, true}, "Money units the vendor starts with, and all it can pay out. Omit it for a purse that never runs out.");
        options.add_property("buys", v8pp::metadata::type{"boolean | undefined", {}, true}, "Whether the vendor takes the player's items at all. On by default; `setBuyPrices` says which ones.");

        auto &stockRow = ServerCatalog().data_type("VendorStockRow", "One thing a vendor sells.");
        stockRow.add_property("item", "string", "The item class, by GUID or by the game's own item name.");
        stockRow.add_property("amount", "number", "How many the vendor has. A row the players buy out disappears.");
        stockRow.add_property("price", "number", "What one costs, in money units -- the amount of the game's `money` item, which is also what `player.giveItem('money', n)` hands out.");

        stockRow.add_property("metadata", v8pp::metadata::type {"Record<string, unknown> | undefined", {}, true}, "Item properties, including custom display overrides and private data. Preserved when bought.");

        auto &buyRow = ServerCatalog().data_type("VendorBuyRow", "One thing a vendor buys from players.");
        buyRow.add_property("item", "string", "The item class, by GUID or by the game's own item name.");
        buyRow.add_property("price", "number", "What the vendor pays for one, in money units.");

        auto &tradeLine = ServerCatalog().data_type("VendorTradeLine", "One settled line of a deal.");
        tradeLine.add_property("metadata", "Record<string, unknown>", "Exact item properties, including private custom data. Sold variants appear as separate lines.");
        tradeLine.add_property("item", "string", "The item class GUID.");
        tradeLine.add_property("name", "string", "The game's own name for the item class.");
        tradeLine.add_property("amount", "number", "How many changed hands.");
        tradeLine.add_property("price", "number", "What one was priced at when the deal settled.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("vendorTrade", "[vendor: number, player: Player, bought: VendorTradeLine[], sold: VendorTradeLine[], balance: number]",
            "Dispatched once a deal has settled: everything in it has already moved, in one commit on the player's inventory whose playerInventoryChanged reason is `trade`. `balance` is what the player came out with in money units -- positive when the vendor paid them. "
            "What the player sold does not join the vendor's stock; add it with `setStock` here if this vendor resells.");
        events.add_property("vendorClosed", "[vendor: number, player: Player, reason: number]",
            "Dispatched when a trading session ends, whoever ended it. 0 the player closed the screen, 1 the server closed it, 2 another vendor replaced it, 3 the client could not bring the screen up, 4 the player left.");

        v8pp::module vendor(isolate, ServerCatalog(), "Vendor",
            "Price lists and purses the server owns, traded on the game's own shop screen.\n\n"
            "A vendor is attached to nothing: open one in front of a player from wherever the gamemode decides the counter is -- `npcInteract`, a dialogue option, a command. "
            "The screen is a preview. Its prices come from here, and pressing Trade sends a basket that the server re-prices, settles and moves itself; nothing the client shows is trusted.\n\n"
            "Prices count in money units, the amount of the game's `money` item.");

        vendor.function("create", &JS_Create, v8pp::metadata::docs("number", {v8pp::metadata::param("options", "VendorOptions", true, "Name, starting purse and whether it buys. All optional.")}, "Creates a vendor with nothing to sell and nothing it buys.", "The vendor id."));

        vendor.function("destroy", &Destroy,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("vendor", "number", false, "The vendor to remove.")}, "Closes every session at the vendor and forgets it. A deal settles the moment its basket arrives, so none is ever left half done.", "False when there was no such vendor."));

        vendor.function("setStock", &JS_SetStock,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("vendor", "number", false, "The vendor to stock."),
                    v8pp::metadata::param("rows", "VendorStockRow[]", false, "Everything it sells, at most 128 rows and each item class once. Replaces the old list."),
                },
                "Replaces what a vendor sells. Every player with its screen open sees the new shelf straight away, and a basket priced against the old one is refused.", "False when there is no such vendor."));

        vendor.function("setBuyPrices", &JS_SetBuyPrices,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("vendor", "number", false, "The vendor to change."),
                    v8pp::metadata::param("rows", "VendorBuyRow[]", false, "Everything it buys, at most 128 rows and each item class once. Replaces the old list."),
                },
                "Replaces what a vendor buys from players and what it pays. Anything not listed is shown at no value and a basket selling it is refused.", "False when there is no such vendor."));

        vendor.function("setPurse", &JS_SetPurse,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("vendor", "number", false, "The vendor to change."),
                    v8pp::metadata::param("purse", "number | null", false, "Money units it holds, or null for a purse that never runs out."),
                },
                "Sets what a vendor can pay out. Deals keep it current: what players pay goes in, what they are paid comes out.", "False when there is no such vendor."));

        vendor.function("getPurse", &JS_GetPurse,
            v8pp::metadata::docs("number | null", {v8pp::metadata::param("vendor", "number", false, "The vendor to ask about.")}, "What a vendor holds.", "Money units, or null for a purse that never runs out or a vendor that does not exist."));

        vendor.function("open", &JS_Open,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("vendor", "number", false, "The vendor to trade with."),
                    v8pp::metadata::param("player", "number", false, "NetworkID of the player to show it to."),
                    v8pp::metadata::param("npc", "number", true, "NetworkID of the NPC who keeps the shop. The screen shows that NPC as the trader; omit it to trade with nobody in particular."),
                },
                "Opens the game's own trade screen on a player. A player already trading has that session closed with reason 2 first, because the game has one trade screen.", "The session id, or 0 when the vendor or the player is gone."));

        vendor.function("close", &Close, v8pp::metadata::docs("boolean", {v8pp::metadata::param("session", "number", false, "The session to end.")}, "Takes the trade screen off that player.", "False when the session had already ended."));

        vendor.function("sessionOf", &SessionOf, v8pp::metadata::docs("number", {v8pp::metadata::param("player", "number", false, "NetworkID of the player to ask about.")}, "The trading session a player is in.", "The session id, or 0 when they are not trading."));

        vendor.function("vendorOf", &VendorOf, v8pp::metadata::docs("number", {v8pp::metadata::param("session", "number", false, "The session to ask about.")}, "The vendor a session trades with.", "The vendor id, or 0 when the session has ended."));

        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Vendor"), vendor.new_instance()).Check();
    }

    void Events::VendorTrade(std::uint32_t vendorId, std::uint64_t playerNetworkId, const std::vector<Features::Vendor::TradeLine> &bought, const std::vector<Features::Vendor::TradeLine> &sold, std::int64_t balance) {
        EmitReservedEvent("vendorTrade", [vendorId, playerNetworkId, &bought, &sold, balance](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, vendorId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(TradeLines(isolate, bought));
            args.push_back(TradeLines(isolate, sold));
            args.push_back(v8pp::to_v8(isolate, static_cast<double>(balance)));
            return args;
        });
    }

    void Events::VendorClosed(std::uint32_t vendorId, std::uint64_t playerNetworkId, std::uint8_t reason) {
        EmitReservedEvent("vendorClosed", [vendorId, playerNetworkId, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, vendorId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, static_cast<std::uint32_t>(reason)));
            return args;
        });
    }
} // namespace KCDC::Scripting
