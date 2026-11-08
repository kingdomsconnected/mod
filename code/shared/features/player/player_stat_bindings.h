#pragma once

#include "shared/features/player/player_stat.h"

#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <string>

namespace KCDC::Scripting {
    // The TypeScript union of a stat table's values: `"health" | "stamina" | ...`.
    template <std::size_t Count>
    std::string PlayerStatType(const std::array<Shared::Game::PlayerStatEntry, Count> &entries) {
        std::string type;
        for (const auto &entry : entries) {
            if (!type.empty()) {
                type += " | ";
            }
            type += "\"" + std::string(entry.value) + "\"";
        }
        return type;
    }

    template <std::size_t Count>
    void RegisterPlayerStatEnum(v8::Isolate *isolate, v8::Local<v8::Object> global, v8pp::metadata::registry &catalog, const char *name, const char *description, const std::array<Shared::Game::PlayerStatEntry, Count> &entries) {
        auto context   = isolate->GetCurrentContext();
        auto object    = v8::Object::New(isolate);
        auto &metadata = catalog.global_object(name, description);
        for (const auto &entry : entries) {
            object->CreateDataProperty(context, v8pp::to_v8(isolate, std::string(entry.name)), v8pp::to_v8(isolate, std::string(entry.value))).Check();
            metadata.add_property(entry.name, "\"" + std::string(entry.value) + "\"", std::string(entry.description), true);
        }
        object->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).Check();
        global->DefineOwnProperty(context, v8pp::to_v8(isolate, name), object, static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();
    }

    inline void RegisterPlayerStatEnums(v8::Isolate *isolate, v8::Local<v8::Object> global, v8pp::metadata::registry &catalog) {
        RegisterPlayerStatEnum(isolate, global, catalog, "PlayerStat", "Writable player stats, in native units. Use getStat and the server's setStat.", Shared::Game::kPlayerStats);
        RegisterPlayerStatEnum(isolate, global, catalog, "DerivedPlayerStat", "Read-only computed player stats. Use getDerivedStat; these values cannot be passed to setStat.", Shared::Game::kDerivedPlayerStats);
    }
} // namespace KCDC::Scripting
