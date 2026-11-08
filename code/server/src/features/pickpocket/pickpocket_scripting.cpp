#include "features/pickpocket/pickpocket_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>

#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        v8::Local<v8::Array> StolenLines(v8::Isolate *isolate, const std::vector<Features::Pickpocket::StolenLine> &lines) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> array     = v8::Array::New(isolate, static_cast<int>(lines.size()));
            for (std::uint32_t index = 0; index < lines.size(); ++index) {
                const Features::Pickpocket::StolenLine &line = lines[index];
                v8::Local<v8::Object> row                    = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "item"), v8pp::to_v8(isolate, std::string(line.item->guid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(line.item->name))).Check();
                row->Set(context, v8pp::to_v8(isolate, "amount"), v8pp::to_v8(isolate, line.amount)).Check();
                array->Set(context, index, row).Check();
            }
            return array;
        }
    } // namespace

    void RegisterPickpocketBindings(v8::Isolate *, v8::Local<v8::Object>) {
        auto &line = ServerCatalog().data_type("PickpocketLine", "One item class that left a victim's pockets for a thief's.");
        line.add_property("item", "string", "The item class GUID.");
        line.add_property("name", "string", "The game's own name for the item class.");
        line.add_property("amount", "number", "How many moved.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerPickpocketStart", "[thief: Player, victim: Player]",
            "Dispatched when a player's charge begins on the game's own Rob entry against another player, before the victim's pockets are opened to them -- the place to decide who may rob whom. "
            "Return `false` from a handler and the attempt is refused: the thief's minigame ends before its loot screen opens, and nothing else follows. Handlers run synchronously, so the decision cannot wait on anything awaited.\n\n"
            "Whether the victim notices is the game's own rule, played on the thief's machine against the victim's body: it turns on the victim's facing and on both players' skills, exactly as against an NPC.");
        events.add_property("playerPickpocketed", "[thief: Player, victim: Player, items: PickpocketLine[]]",
            "Dispatched once a theft has settled: everything in `items` has already left the victim and reached the thief. "
            "The thief's loot screen is only a claim -- each line was taken from the victim first and only what really came out was handed over -- so `items` can be shorter than what the thief took, or empty when they closed the screen with nothing. "
            "Nothing else happens to either player: whether this is a crime, and what it costs, is the gamemode's.");
        events.add_property("playerPickpocketCaught", "[thief: Player, victim: Player]",
            "Dispatched when the victim noticed the thief, while charging or with the loot screen up. Nothing was taken, and no NPC reacts on the victim's behalf: the gamemode decides what being caught means.");
    }

    bool Events::PlayerPickpocketStart(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId) {
        return EmitReservedEventSync("playerPickpocketStart", [thiefNetworkId, victimNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, thiefNetworkId));
            args.push_back(WrapPlayer(isolate, victimNetworkId));
            return args;
        });
    }

    void Events::PlayerPickpocketed(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId, const std::vector<Features::Pickpocket::StolenLine> &lines) {
        EmitReservedEvent("playerPickpocketed", [thiefNetworkId, victimNetworkId, &lines](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, thiefNetworkId));
            args.push_back(WrapPlayer(isolate, victimNetworkId));
            args.push_back(StolenLines(isolate, lines));
            return args;
        });
    }

    void Events::PlayerPickpocketCaught(std::uint64_t thiefNetworkId, std::uint64_t victimNetworkId) {
        EmitReservedEvent("playerPickpocketCaught", [thiefNetworkId, victimNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, thiefNetworkId));
            args.push_back(WrapPlayer(isolate, victimNetworkId));
            return args;
        });
    }
} // namespace KCDC::Scripting
