#include "features/player/player_buff_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"

#include "shared/features/player/player_buff_bindings.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/custom_buff_registry.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /**
         * The classes a server can take over, which is the only list worth
         * publishing. Of the game's 22, most are not a decision anyone can
         * take -- `perk` shadows the perk list, `item` follows equipment,
         * `system` is plumbing down to encumbrance -- so they are neither
         * listed nor accepted.
         */
        std::vector<std::string> ClaimableClassNames() {
            std::vector<std::string> names;
            for (std::uint8_t id = 0; id < Shared::Game::BuffClass::kCount; ++id) {
                if (Shared::Game::IsClaimableBuffClass(id)) {
                    names.emplace_back(Shared::Game::BuffClass::Name(id));
                }
            }
            return names;
        }

        std::vector<std::string> TagNames() {
            std::vector<std::string> names;
            for (const std::string_view &tag : Shared::Game::g_BuffAiTags) {
                names.emplace_back(tag);
            }
            return names;
        }
    } // namespace

    // JS: Buffs.register(id, definition) -> string
    void Buffs::JS_Register(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        try {
            if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsObject()) {
                throw std::invalid_argument("Buffs.register requires id and definition");
            }
            const auto json = StringifyValue(isolate, info[1]);
            if (!json || json->size() > 12000) {
                throw std::invalid_argument("Invalid or oversized buff definition");
            }
            const auto id         = v8pp::from_v8<std::string>(isolate, info[0]);
            const bool registered = Shared::Game::CustomBuffRegistry::Get().Find(id) != nullptr;
            const auto &buff      = Shared::Game::CustomBuffRegistry::Get().Register(id, nlohmann::json::parse(*json));
            if (!registered) {
                Server::Get()->GetBuffs().PublishDefinition(buff.id);
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, buff.guid));
        }
        catch (const std::exception &error) {
            isolate->ThrowError(v8pp::to_v8(isolate, error.what()));
        }
    }

    // JS: Buffs.find(query) -> BuffInfo | null
    void Buffs::JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Buffs.find: expected (query)"));
            return;
        }

        const Shared::Game::BuffEntry *entry = Shared::Game::ResolveBuff(v8pp::from_v8<std::string>(isolate, info[0]));
        if (entry == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(BuffInfoToV8(isolate, *entry));
    }

    // JS: Buffs.claim(classNames) -> string[]
    void Buffs::JS_Claim(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        // The array is required: an optional one made `Buffs.claim()` read as
        // "claim everything" while releasing everything.
        if (info.Length() < 1 || !info[0]->IsArray()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Buffs.claim: expected (classNames) as an array; Buffs.claim([]) hands every class back"));
            return;
        }

        std::uint32_t mask = 0;
        for (const std::string &name : v8pp::from_v8<std::vector<std::string>>(isolate, info[0])) {
            const int classId = Shared::Game::BuffClass::Find(name);

            // Claiming `perk` would strip every perk on the local body,
            // silently, on the one machine nobody is watching.
            if (classId < 0 || !Shared::Game::IsClaimableBuffClass(static_cast<std::uint8_t>(classId))) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Buffs.claim: '" + name + "' is not a class a server can decide; see Buffs.classes"));
                return;
            }
            mask |= Shared::Game::BuffClass::Bit(static_cast<std::uint8_t>(classId));
        }

        auto *server = Server::Get();
        if (server == nullptr) {
            info.GetReturnValue().Set(v8pp::to_v8(isolate, std::vector<std::string>()));
            return;
        }
        server->GetBuffs().ClaimClasses(mask);

        std::vector<std::string> claimed;
        for (std::uint8_t id = 0; id < Shared::Game::BuffClass::kCount; ++id) {
            if ((mask & Shared::Game::BuffClass::Bit(id)) != 0) {
                claimed.emplace_back(Shared::Game::BuffClass::Name(id));
            }
        }
        info.GetReturnValue().Set(v8pp::to_v8(isolate, claimed));
    }

    void Buffs::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerCustomBuffApply", "[player: Player, buff: BuffInfo]", "A native custom effect instance attached. A refresh and each additional stack raise this event after the owner reports them.");
        events.add_property("playerCustomBuffTick", "[player: Player, buff: BuffInfo]",
            "A reported active custom effect reached its tickInterval on the server clock. At most one callback per instance per server update; missed ticks are skipped. Native modifiers and duration use the owner's simulation clock.");
        events.add_property("playerCustomBuffRemove", "[player: Player, buff: BuffInfo, reason: \"server\" | \"expired\" | \"refreshed\" | \"disconnect\"]",
            "One custom instance left the body. Expired also covers native removal. Disconnect clears lifecycle callbacks for this connection.");
        auto &definition = ServerCatalog().data_type("CustomBuffDefinition", "Immutable custom effect, with an existing native buff supplying its icon.");
        definition.add_property("name", "string", "Display name; 1 to 256 UTF-8 bytes.");
        definition.add_property("description", v8pp::metadata::type {"string", {}, true}, "Optional description, up to 4096 UTF-8 bytes.");
        definition.add_property("visual", "string", "Existing game buff name or GUID; only presentation is borrowed.");
        definition.add_property("duration", v8pp::metadata::type {"number", {}, true}, "Positive real seconds up to seven days, or -1 for permanent; default -1.");
        definition.add_property("tickInterval", v8pp::metadata::type {"number", {}, true}, "Server lifecycle callback interval in seconds; zero disables, otherwise 0.1 to 86400. Default zero.");
        definition.add_property("stackable", v8pp::metadata::type {"boolean", {}, true}, "Default false. When true, each application has its own native timer and modifiers.");
        definition.add_property("maxStacks", v8pp::metadata::type {"number", {}, true}, "Integer 1 to 16; default 1.");
        definition.add_property("refresh", v8pp::metadata::type {"boolean", {}, true}, "Default true. At the stack limit, replaces the oldest instance with a full-duration instance; false ignores applications at the limit.");
        definition.add_property("modifiers", v8pp::metadata::type {"Record<string, number | { operation: \"add\" | \"multiply\" | \"set\"; value: number }>", {}, true},
            "Optional native modifiers. A number adds points. Buffs.modifiers lists supported names. healthRegeneration adds health per second. Effective stat changes do not change earned XP.");
        events.add_property("playerBuffAdded", "[player: Player, buff: string, source: \"server\" | \"native\"]",
            "Dispatched when a status effect appears on a player's body. `source` says whether this server added it; `native` means the game did -- a potion they drank, an injury they took. It fires for whatever they are already carrying when their client first reports in, so a handler sees the full picture without asking for it.");
        events.add_property("playerBuffRemoved", "[player: Player, buff: string, reason: \"server\" | \"expired\"]",
            "Dispatched when a status effect leaves a player's body. `server` is a removal this server asked for; `expired` is everything else -- it ran out, or the game replaced it. Effect timers run on each player's own machine, so the server never expires one itself.");
        events.add_property("playerBuffBlocked", "[player: Player, buff: string]",
            "Dispatched when the game tried to give a player an effect of a kind this server claimed, and their client turned it down. This is the other half of `Buffs.claim`: the drink was still drunk and the item still consumed, so the handler decides what really happens -- usually `player.addBuff` with the resource's own rule applied. Nothing raises it until something is claimed, and repeats of the same effect are limited to twice a second per player.");

        RegisterBuffDataTypes(ServerCatalog());

        v8pp::module buffs(isolate, ServerCatalog(), "Buffs", "The game's status-effect table, and which of it this server decides.");
        buffs.property(
            "modifiers",
            [] {
                return Shared::Game::CustomBuffRegistry::ModifierNames();
            },
            v8pp::metadata::property_docs("string[]",
                "Supported custom modifier names: progression tracks, charisma, maxStamina (stamina alias), maxHealth, carryCapacity, runSpeed, armorRating, bodyArmor, hungerDrain, energyDrain, staminaRegeneration and healthRegeneration. Native query bounds and contexts still apply."));
        buffs.function("register", &Buffs::JS_Register,
            v8pp::metadata::docs("string",
                {v8pp::metadata::param("id", "string", false, "Unique resource-qualified identifier, such as my_resource:blessing."),
                    v8pp::metadata::param("definition", "CustomBuffDefinition", false, "Immutable definition; unknown keys, unsupported modifiers and invalid values throw.")},
                "Registers a custom native effect and publishes it to connected and future clients. An identical registration is idempotent; an existing id with another definition throws. Returns a GUID accepted by the existing player buff methods, like Items.register.",
                "Custom buff GUID."));

        buffs.property(
            "classes",
            []() {
                return ClaimableClassNames();
            },
            v8pp::metadata::property_docs("string[]",
                "The kinds of effect a server can take over, which is what `claim` accepts: the ones carrying state nobody can work out for themselves -- potions, poison, injury, drunkenness, illness, unconsciousness. The game has other kinds, and they read back in `BuffInfo.class`, but they either shadow something that already replicates or are the game's own plumbing, so there is no decision in them to take."));

        buffs.property(
            "tags",
            []() {
                return TagNames();
            },
            v8pp::metadata::property_docs("string[]", "The families the game groups effects into, which is what `player.clearBuffs` takes: `poison`, `bleed`, `alcohol_drunk`, `unconscious` and nineteen more. These are the game's own words, and its own AI reasons in them."));

        buffs.function("find", &Buffs::JS_Find,
            v8pp::metadata::docs("BuffInfo | null", {v8pp::metadata::param("query", "string", false, "An effect's name, as the game's buff tables spell it, or its GUID.")},
                "Looks a status effect up, so a resource can check a name is real before handing it to a player and can see what the effect is before applying it.", "What the tables say about it, or null for a name they do not carry."));

        buffs.function("claim", &Buffs::JS_Claim,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("classNames", "string[]", false, "The kinds of effect to decide, from `Buffs.classes`. `[]` hands them all back. Throws for anything else.")},
                "Takes over whole kinds of effect. Every client then turns down an effect of that kind the game tried to give its own player, and raises `playerBuffBlocked` instead -- which is where the resource decides what really happens, usually `player.addBuff` with its own rule applied. A claim nothing listens to does not move the gameplay here, it removes it.\n\nTwo things to plan around. The refusal covers the game's follow-on effects too: claim `alcohol` and the six `alcoholism_level*` steps stop arriving along with the drink, so the resource owns the whole progression. And an effect has no strength -- the handler can block it, swap it for another, or let it through, but it cannot make the drink half as strong.\n\nReplaces the previous claim rather than adding to it, and reaches every connected client at once.",
                "The kinds now claimed."));

        buffs.publish(global);
    }

    void Events::PlayerBuffAdded(std::uint64_t playerNetworkId, const std::string &buff, bool fromServer) {
        EmitReservedEvent("playerBuffAdded", [playerNetworkId, &buff, fromServer](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, buff));
            args.push_back(v8pp::to_v8(isolate, std::string(fromServer ? "server" : "native")));
            return args;
        });
    }

    void Events::PlayerCustomBuff(std::uint64_t playerNetworkId, const std::string &buff, const char *event, const std::string &reason) {
        const auto *definition = Shared::Game::CustomBuffRegistry::Get().Find(buff);
        if (definition == nullptr) {
            return;
        }
        EmitReservedEvent(event, [playerNetworkId, definition, &reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args {WrapPlayer(isolate, playerNetworkId), BuffInfoToV8(isolate, definition->entry)};
            if (!reason.empty()) {
                args.push_back(v8pp::to_v8(isolate, reason));
            }
            return args;
        });
    }

    void Events::PlayerBuffRemoved(std::uint64_t playerNetworkId, const std::string &buff, const std::string &reason) {
        EmitReservedEvent("playerBuffRemoved", [playerNetworkId, &buff, &reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, buff));
            args.push_back(v8pp::to_v8(isolate, reason));
            return args;
        });
    }

    void Events::PlayerBuffBlocked(std::uint64_t playerNetworkId, const std::string &buff) {
        EmitReservedEvent("playerBuffBlocked", [playerNetworkId, &buff](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, buff));
            return args;
        });
    }
} // namespace KCDC::Scripting
