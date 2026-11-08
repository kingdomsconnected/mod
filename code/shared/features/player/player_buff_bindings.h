#pragma once

#include "shared/features/player/player_buff.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/custom_buff_registry.h"

#include <scripting/scripting_catalog.h>

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <cstdint>
#include <string>
#include <vector>

// The two shapes the buff bindings hand back, and the functions that build
// them. `BuffInfo` is what the tables say about an effect; `BuffState` is the
// same fields plus the two only a live body can answer.
namespace KCDC::Scripting {
    namespace Detail {
        inline void AddBuffInfoProperties(v8pp::metadata::symbol &type) {
            type.add_property("name", "string", "The name the game's own buff tables give the effect, and the spelling every buff verb takes.");
            type.add_property("class", "string", "The kind of effect it is -- `poison`, `alcohol`, `injury`, `potion`. `Buffs.classes` lists the ones a server can take over.");
            type.add_property("aiTag", "string | null", "The family `player.clearBuffs` would take it off with, or null for the majority that belong to none.");
            type.add_property("duration", "number", "How long the effect runs from start to finish, in real seconds, and negative for one with no end.");
            type.add_property("custom", "boolean", "Whether this definition was registered with Buffs.register.");
            type.add_property("displayName", "string | null", "Registered display text for a custom effect; null for a native effect.");
            type.add_property("description", "string | null", "Registered description for a custom effect; null for a native effect.");
        }
    } // namespace Detail

    inline void RegisterBuffDataTypes(v8pp::metadata::registry &catalog) {
        auto &info = catalog.data_type("BuffInfo", "What the game's own tables say about one status effect.");
        Detail::AddBuffInfoProperties(info);

        auto &state = catalog.data_type("BuffState", "One status effect currently on a body: everything `BuffInfo` says about it, plus how long it has been there and who put it there.");
        Detail::AddBuffInfoProperties(state);
        state.add_property("since", "number",
            "How long it has been on the body, in real seconds. `duration - since` is roughly what is left, and roughly is the best anyone can do: the game advances buff time from each client's own frame delta. 0 for an effect that was already there when the player connected.");
        state.add_property("source", "\"server\" | \"native\"", "Whether this server put it there, or the game did -- a potion they drank, an injury they took.");
    }

    namespace Detail {
        /** The `BuffInfo` fields, onto an object either shape is being built in. */
        inline void WriteBuffInfo(v8::Isolate *isolate, v8::Local<v8::Object> out, const Shared::Game::BuffEntry &entry) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const bool tagged              = entry.aiTag >= 0 && static_cast<std::size_t>(entry.aiTag) < Shared::Game::g_BuffAiTags.size();

            out->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(entry.name))).Check();
            out->Set(context, v8pp::to_v8(isolate, "class"), v8pp::to_v8(isolate, std::string(Shared::Game::BuffClass::Name(entry.classId)))).Check();
            out->Set(context, v8pp::to_v8(isolate, "aiTag"), tagged ? v8pp::to_v8(isolate, std::string(Shared::Game::g_BuffAiTags[entry.aiTag])) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            out->Set(context, v8pp::to_v8(isolate, "duration"), v8pp::to_v8(isolate, Shared::Game::BuffDurationSeconds(entry))).Check();
            const auto *custom = Shared::Game::CustomBuffRegistry::Get().Find(entry.guid);
            out->Set(context, v8pp::to_v8(isolate, "custom"), v8pp::to_v8(isolate, custom != nullptr)).Check();
            out->Set(context, v8pp::to_v8(isolate, "displayName"), custom != nullptr ? v8pp::to_v8(isolate, custom->name) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            out->Set(context, v8pp::to_v8(isolate, "description"), custom != nullptr ? v8pp::to_v8(isolate, custom->description) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
        }
    } // namespace Detail

    /** One catalog row as a `BuffInfo`. */
    inline v8::Local<v8::Object> BuffInfoToV8(v8::Isolate *isolate, const Shared::Game::BuffEntry &entry) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Object> out = v8::Object::New(isolate);
        Detail::WriteBuffInfo(isolate, out, entry);
        return handleScope.Escape(out);
    }

    /** Everything a body is wearing, as `player.buffs` hands it back. `nowMs` is the caller's own clock, the one `firstSeen` was taken from. */
    inline v8::Local<v8::Array> BuffStatesToV8(v8::Isolate *isolate, const std::vector<Shared::RPC::ObservedBuff> &reported, std::uint64_t nowMs) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> out       = v8::Array::New(isolate);

        std::uint32_t written = 0;
        for (const Shared::RPC::ObservedBuff &observed : reported) {
            const Shared::Game::BuffEntry *row = Shared::Game::FindBuffById(observed.buff.buff);
            if (row == nullptr) {
                // A definition this build's tables do not carry. There is
                // nothing true to say about it, so say nothing.
                continue;
            }

            v8::Local<v8::Object> state = v8::Object::New(isolate);
            Detail::WriteBuffInfo(isolate, state, *row);
            state->Set(context, v8pp::to_v8(isolate, "since"), v8pp::to_v8(isolate, nowMs > observed.firstSeen ? static_cast<double>(nowMs - observed.firstSeen) / 1000.0 : 0.0)).Check();
            state->Set(context, v8pp::to_v8(isolate, "source"), v8pp::to_v8(isolate, std::string(observed.buff.source == Shared::RPC::BuffSource::Server ? "server" : "native"))).Check();
            out->Set(context, written++, state).Check();
        }
        return handleScope.Escape(out);
    }
} // namespace KCDC::Scripting
