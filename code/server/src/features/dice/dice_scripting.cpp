#include "features/dice/dice_scripting.h"
#include "features/smart_object/smart_object_service.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/dice/dice_service.h"
#include "shared/game/dice_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        void Tables(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const MafiaNet::VirtualWorldId virtualWorld = info.Length() > 0 && info[0]->IsNumber() ? static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[0])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;
            nlohmann::json rows = nlohmann::json::array();
            for (const Shared::Game::DiceTable *table : KCDC::Features::SmartObject::SmartObjectService::Get().DiceTables(virtualWorld)) {
                const bool placed = Shared::Game::IsSmartObjectGuid(table->guid);
                rows.push_back({{"id", FormatLevelGuid(table->guid)}, {"position", {{"x", table->x}, {"y", table->y}, {"z", table->z}}}, {"seats", {FormatLevelGuid(table->firstSeatGuid), FormatLevelGuid(table->secondSeatGuid)}}, {"placed", placed}});
            }
            v8::Local<v8::Value> result;
            if (v8::JSON::Parse(info.GetIsolate()->GetCurrentContext(), v8pp::to_v8(info.GetIsolate(), rows.dump()).As<v8::String>()).ToLocal(&result)) {
                info.GetReturnValue().Set(result);
            }
        }

        // JS: Dice.start(first, second, options?) -> number
        void JS_Start(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 2 || !info[0]->IsNumber() || !info[1]->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Dice.start takes a player id, a player or NPC id, and optionally an options object")));
                return;
            }
            std::uint32_t targetScore = 0;
            std::array<std::string, 2> names;
            if (info.Length() >= 3 && info[2]->IsObject()) {
                const char *keys[] = {"firstName", "secondName"};
                for (std::size_t i = 0; i < 2; ++i) {
                    const auto name = OptionField(isolate, info[2].As<v8::Object>(), keys[i]);
                    if (name.IsEmpty() || name->IsUndefined()) {
                        continue;
                    }
                    if (!name->IsString()) {
                        isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Dice.start: names must be strings")));
                        return;
                    }
                    names[i] = v8pp::from_v8<std::string>(isolate, name);
                }
                const v8::Local<v8::Value> target = OptionField(isolate, info[2].As<v8::Object>(), "targetScore");
                if (!target.IsEmpty() && !target->IsUndefined()) {
                    const double value = target->IsNumber() ? v8pp::from_v8<double>(isolate, target) : -1.0;
                    if (!std::isfinite(value) || value < 1.0 || value != std::floor(value)) {
                        isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Dice.start: targetScore must be a whole number from 1")));
                        return;
                    }
                    targetScore = static_cast<std::uint32_t>(std::min(value, 4294967295.0));
                }
            }

            std::string error;
            const std::uint32_t matchId = Features::Dice::DiceService::Get().Start(v8pp::from_v8<std::uint64_t>(isolate, info[0]), v8pp::from_v8<std::uint64_t>(isolate, info[1]), targetScore, error, names);
            if (matchId == 0) {
                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "Dice.start: " + error)));
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, matchId));
        }

        bool SetNames(std::uint32_t matchId, const std::string &first, const std::string &second) {
            if (!Shared::RPC::DiceDetail::ValidName(first) || !Shared::RPC::DiceDetail::ValidName(second)) {
                throw std::invalid_argument("Dice.setNames: names must contain at most 64 UTF-8 bytes and no control characters");
            }
            return Features::Dice::DiceService::Get().SetNames(matchId, {first, second});
        }

        bool Move(std::uint32_t matchId, std::uint32_t heldMask, bool pass) {
            return heldMask <= Shared::Game::Dice::kAllDiceMask && Features::Dice::DiceService::Get().Move(matchId, static_cast<std::uint8_t>(heldMask), pass);
        }

        bool Stop(std::uint32_t matchId) {
            return Features::Dice::DiceService::Get().Stop(matchId);
        }

        std::uint32_t MatchOf(std::uint64_t playerId) {
            return Features::Dice::DiceService::Get().MatchOf(playerId);
        }
    } // namespace

    void RegisterDiceBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &table = ServerCatalog().data_type("DiceTableInfo", "A dice table placed by the current level. The catalog includes quest-layer tables that may not currently be loaded on a client.");
        table.add_property("id", "string", "The table's GUID as sixteen lowercase hex digits: the level's own, or the one a placed table's clients build it under.");
        table.add_property("placed", "boolean", "Whether a script or a World Builder export placed the table, rather than the level.");
        table.add_property("position", "Vector3", "The world-space midpoint between the table's two seat alignment points, in metres. The behavior entity itself may be offset from this interaction centre.");
        table.add_property("seats", "[string, string]", "The two sitting smart-object GUIDs, in native seat order. Compare with a seated player's seatGuid; these are not the minigame's alignment tagpoints.");
        // A declared type, not an inline shape: the contract only carries
        // named ones, and `Dice` itself is the global module's name.
        auto &options = ServerCatalog().data_type("DiceMatchOptions", "How a dice match is played.");
        options.add_property("targetScore", v8pp::metadata::type{"number | undefined", {}, true}, "The score that wins, banked at the end of a turn. 2000 when omitted, at most 100000.");

        options.add_property("firstName", v8pp::metadata::type {"string | undefined", {}, true}, "First seat's scoreboard label, up to 64 UTF-8 bytes without control characters. Empty or omitted uses the native label.");
        options.add_property("secondName", v8pp::metadata::type {"string | undefined", {}, true}, "Second seat's scoreboard label. Each client maps the seat to its local or opponent column.");
        auto &progress = ServerCatalog().data_type("DiceProgressEvent", "An accepted server dice action. Notifications describe server decisions, which can precede the native animation. Seats and die-mask bits start at zero; turns and rolls start at one.");
        progress.add_property("match", "number", "Match ID.");
        progress.add_property("player", "number", "Network ID of the acting player, or of the NPC on the second seat.");
        progress.add_property("seat", "number", "Acting seat: 0 for first, 1 for second.");
        progress.add_property("turn", "number", "Turn number across both seats, starting at 1.");
        progress.add_property("roll", "number", "Roll within the turn; 0 before its first roll.");
        progress.add_property("moveIndex", "number", "Number of accepted moves so far.");
        progress.add_property("turnScore", "number", "Unbanked points in this turn, before they are lost on a bust or banked on a pass.");
        progress.add_property("totalScore", "number", "This seat's banked score.");
        progress.add_property("faces", "number[]", "Six pip values (1 through 6) in die-index order on diceRoll; empty on other events. rolledMask identifies the dice rolled this time.");
        progress.add_property("rolledMask", "number", "Bit mask of dice rolled, on diceRoll; zero otherwise.");
        progress.add_property("heldMask", "number", "Bit mask of dice selected in the accepted move; zero otherwise.");
        progress.add_property("points", "number", "Points added by diceMove or banked by a passed diceTurnEnd; zero otherwise.");
        progress.add_property("reason", "string", "continue or pass on diceMove; bust or rolled on diceRoll; pass, bust, won, gaveUp, left, timedOut, failed, or stopped on diceTurnEnd; empty on diceTurnStart.");
        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("diceTurnStart", "[event: DiceProgressEvent]", "A new turn begins, including turns after automatic busts.");
        events.add_property("diceRoll", "[event: DiceProgressEvent]",
            "The server dealt a roll. Includes all six face values and the mask rolled this time. When `player` is the NPC on the second seat and `reason` is `rolled`, the decision is the script's: answer with Dice.move.");
        events.add_property("diceMove", "[event: DiceProgressEvent]", "The server accepted Continue or Pass, from a player or through Dice.move. Invalid or repeated requests emit nothing.");
        events.add_property("diceTurnEnd", "[event: DiceProgressEvent]", "A turn ended by banking, busting, winning, or a match ending. A round means one player's turn.");
        events.add_property("diceMatchStart", "[match: number, first: Player, second: Player | Npc, targetScore: number]",
            "Dispatched once a match has started and its players have been sent to the table. `second` is the Npc when one holds the second seat. Which seat throws first is drawn at random.");
        events.add_property("diceMatchEnd", "[match: number, first: Player | null, second: Player | Npc | null, winner: number, reason: string, firstScore: number, secondScore: number]",
            "Dispatched once a match is over and both players have been told. `winner` is 0 when `first` won, 1 when `second` did, 255 when nobody did. "
            "`reason` is `won` (a seat banked the target score), `gaveUp` (a player left the table), `left` (a player disconnected, or the NPC was removed or died), "
            "`timedOut` (the seat to throw, player or NPC, did not move for three minutes), `failed` (a client could not play the table) or `stopped` (`Dice.stop`). "
            "A player or NPC who has gone since is null. Nothing changes hands on its own: what a match was played for is the gamemode's to settle here.");

        v8pp::module dice(isolate, ServerCatalog(), "Dice",
            "Dice matches between two players, or a player and a server NPC, on the game's own dice table.\n\n"
            "Both players sit down at the dice table nearest them and play it as they would against an NPC, the other player's body opposite. "
            "Every throw is dealt and every move checked by the server, by the game's own scoring rules; badges and dice perks are off, so a match is decided by the dice and the decisions alone.\n\n"
            "An Npc may hold the second seat. Its body sits opposite the player, its throws are dealt like anyone's, and its decisions are the script's: `diceRoll` says what it threw, `Dice.move` plays its move.");

        dice.function("tables", &Tables,
            v8pp::metadata::docs("DiceTableInfo[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world whose tables to list; the global one when omitted.")},
                "Lists the dice tables players in a virtual world can play at, and their seats: the level's own, minus any a level edit took out of that world, and every table `SmartObject.spawn` or a World Builder export placed there or in the global world. Readiness and occupancy belong to the gamemode.",
                "The tables, in no particular order."));
        dice.function("start", &JS_Start,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("first", "number", false, "NetworkID of the player on the table's first seat."),
                    v8pp::metadata::param("second", "number", false, "NetworkID of the player on the second seat, or of a living human Npc to seat there and play through Dice.move."),
                    v8pp::metadata::param("options", "DiceMatchOptions", true, "The target score. Optional."),
                },
                "Starts a match. Both players must stand together, within a few metres, at a dice table, and neither may be playing already. "
                "An NPC is held to the same: it must stand at the table with the player, in the same virtual world. Throws an Error saying why when it cannot start.",
                "The match id."));
        dice.function("move", &Move,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("match", "number", false, "Match ID."),
                    v8pp::metadata::param("heldMask", "number", false, "Bit mask of the dice to set aside, bit 0 for die 0 through bit 5 for die 5, among those `diceRoll` reported in `rolledMask`. Every die in it must count towards a scoring combination."),
                    v8pp::metadata::param("pass", "boolean", false, "True banks the turn after scoring the held dice, if any. False scores them, which needs at least one, and throws the rest."),
                },
                "Plays the move of the NPC on the second seat, checked by the rules a player's move is. Legal only while it is the NPC's turn and its throw is on the table, so after a `diceRoll` naming it with reason `rolled`; calling it from that handler is supported. "
                "An accepted move raises `diceMove` and what follows from it. The NPC forfeits like a player when no move is accepted for three minutes.",
                "False, with nothing changed, when the match has ended or has no NPC seat, it is not the NPC's turn, or the move is not legal."));

        dice.function("setNames", &SetNames,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("match", "number", false, "Match ID."), v8pp::metadata::param("first", "string", false, "First seat label."), v8pp::metadata::param("second", "string", false, "Second seat label.")},
                "Changes both scoreboard labels on the next native scoreboard refresh. Empty restores a native label. Names allow at most 64 UTF-8 bytes and no control characters.", "False if the match has ended."));
        dice.function("stop", &Stop, v8pp::metadata::docs("boolean", {v8pp::metadata::param("match", "number", false, "The match to end.")}, "Ends a match with nobody winning; both tables close.", "False when it had already ended."));

        dice.function("matchOf", &MatchOf, v8pp::metadata::docs("number", {v8pp::metadata::param("player", "number", false, "NetworkID of the player to ask about.")}, "The match a player is in.", "The match id, or 0."));

        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Dice"), dice.new_instance()).Check();
    }

    void Events::DiceProgress(const char *event, const nlohmann::json &data) {
        EmitReservedEvent(event, [&](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            v8::Local<v8::Value> value;
            if (v8::JSON::Parse(isolate->GetCurrentContext(), v8pp::to_v8(isolate, data.dump()).As<v8::String>()).ToLocal(&value)) {
                args.push_back(value);
            }
            return args;
        });
    }

    void Events::DiceMatchStart(std::uint32_t matchId, std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint32_t targetScore) {
        EmitReservedEvent("diceMatchStart", [matchId, firstNetworkId, secondNetworkId, targetScore](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, matchId));
            args.push_back(WrapPlayer(isolate, firstNetworkId));
            args.push_back(WrapCombatant(isolate, secondNetworkId));
            args.push_back(v8pp::to_v8(isolate, targetScore));
            return args;
        });
    }

    void Events::DiceMatchEnd(std::uint32_t matchId, std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint8_t winnerSeat, const char *reason, std::uint32_t firstScore, std::uint32_t secondScore) {
        EmitReservedEvent("diceMatchEnd", [=](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, matchId));
            args.push_back(WrapPlayer(isolate, firstNetworkId));
            args.push_back(WrapCombatant(isolate, secondNetworkId));
            args.push_back(v8pp::to_v8(isolate, static_cast<std::uint32_t>(winnerSeat)));
            args.push_back(v8pp::to_v8(isolate, std::string(reason)));
            args.push_back(v8pp::to_v8(isolate, firstScore));
            args.push_back(v8pp::to_v8(isolate, secondScore));
            return args;
        });
    }
} // namespace KCDC::Scripting
