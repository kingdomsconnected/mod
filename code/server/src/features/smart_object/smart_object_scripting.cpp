#include "smart_object_scripting.h"
#include "smart_object_service.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include "shared/scripting/level_guid.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        KCDC::Features::SmartObject::SmartObjectService &Objects() {
            return KCDC::Features::SmartObject::SmartObjectService::Get();
        }

        SmartObject *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<SmartObject>::unwrap_object(info.GetIsolate(), info.This());
        }

        MafiaNet::VirtualWorldId WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return MafiaNet::VIRTUAL_WORLD_GLOBAL;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        std::optional<MafiaNet::VirtualWorldId> OptionalWorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsNumber() ? std::optional<MafiaNet::VirtualWorldId>(WorldArg(info, index)) : std::nullopt;
        }

        /** The parts a body sits or lies on: every `StanceSmartObject`, and the seats a dice table links. */
        std::vector<std::size_t> SeatParts(const Shared::Game::SmartObjectBlueprint &blueprint) {
            std::vector<std::size_t> seats;
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                if (blueprint.parts[index].entityClass == "StanceSmartObject") {
                    seats.push_back(index);
                }
            }
            if (blueprint.stationPart >= 0) {
                const Shared::Game::SmartObjectPart &station = blueprint.parts[blueprint.stationPart];
                for (std::size_t link = 0; link < station.linkCount; ++link) {
                    if (station.links[link].name == "seat1_SO" || station.links[link].name == "seat2_SO") {
                        seats.push_back(station.links[link].target);
                    }
                }
            }
            return seats;
        }

        v8::Local<v8::Value> JsonValue(v8::Isolate *isolate, const nlohmann::json &value) {
            return ParseJsonValue(isolate, value.dump(), v8::Null(isolate));
        }

        // JS: SmartObject.spawn(definition, virtualWorld?) -> SmartObject
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            const std::optional<std::string> text = info.Length() > 0 && info[0]->IsObject() ? StringifyValue(isolate, info[0]) : std::nullopt;
            if (!text.has_value()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SmartObject.spawn: expected (definition, virtualWorld?)"));
                return;
            }
            Shared::Game::SmartObjectDefinition definition;
            std::string error;
            if (!Shared::Game::SmartObjectDefinitionFromJson(nlohmann::json::parse(*text, nullptr, false), definition, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SmartObject.spawn: " + error));
                return;
            }
            Shared::Entities::SmartObjectEntity *object = Objects().Spawn(definition, WorldArg(info, 1), error);
            if (object == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SmartObject.spawn: " + error));
                return;
            }
            info.GetReturnValue().Set(WrapSmartObject(isolate, object->GetNetworkID()));
        }

        // JS: SmartObject.all(virtualWorld?) -> SmartObject[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> objects   = v8::Array::New(isolate);
            std::uint32_t index            = 0;
            for (auto *object : Objects().Collect(OptionalWorldArg(info, 0))) {
                objects->Set(context, index++, WrapSmartObject(isolate, object->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(objects);
        }

        // JS: SmartObject.getById(id) -> SmartObject | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SmartObject.getById: expected (id)"));
                return;
            }
            info.GetReturnValue().Set(WrapSmartObject(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: SmartObject.blueprints() -> SmartObjectBlueprintInfo[]
        void JS_Blueprints(const v8::FunctionCallbackInfo<v8::Value> &info) {
            nlohmann::json rows = nlohmann::json::array();
            for (const Shared::Game::SmartObjectBlueprint &blueprint : Shared::Game::g_SmartObjectBlueprints) {
                rows.push_back({{"name", blueprint.name}, {"kind", Shared::Game::SmartObjectKindName(blueprint.kind)}, {"seats", SeatParts(blueprint).size()}, {"bare", Shared::Game::SmartObjectStandsBare(blueprint)}});
            }
            info.GetReturnValue().Set(JsonValue(info.GetIsolate(), rows));
        }

        // JS: SmartObject.stations(virtualWorld?) -> SmartObjectStation[]
        void JS_Stations(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> rows      = v8::Array::New(isolate);
            std::uint32_t index            = 0;
            for (const auto &station : Objects().Stations(WorldArg(info, 0))) {
                v8::Local<v8::Object> row = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, FormatLevelGuid(station.guid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, Shared::Game::SmartObjectKindName(station.kind))).Check();
                row->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, station.position)).Check();
                row->Set(context, v8pp::to_v8(isolate, "placed"), v8pp::to_v8(isolate, station.smartObjectId != 0)).Check();
                row->Set(context, v8pp::to_v8(isolate, "smartObject"), station.smartObjectId != 0 ? WrapSmartObject(isolate, station.smartObjectId) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
                rows->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(rows);
        }

        std::uint64_t GuidArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage) {
            v8::Isolate *isolate     = info.GetIsolate();
            const std::uint64_t guid = info.Length() > 0 && info[0]->IsString() ? ParseLevelGuid(v8pp::from_v8<std::string>(isolate, info[0])) : 0;
            if (guid == 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": expected (id, virtualWorld?)"));
            }
            return guid;
        }

        // JS: SmartObject.remove(id, virtualWorld?) -> void
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate     = info.GetIsolate();
            const std::uint64_t guid = GuidArg(info, "SmartObject.remove");
            if (guid == 0) {
                return;
            }
            std::string error;
            if (!Objects().Remove(guid, WorldArg(info, 1), error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SmartObject.remove: " + error));
            }
        }

        // JS: SmartObject.restore(id, virtualWorld?) -> boolean
        void JS_Restore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::uint64_t guid = GuidArg(info, "SmartObject.restore");
            if (guid == 0) {
                return;
            }
            info.GetReturnValue().Set(Objects().Unremove(guid, WorldArg(info, 1)));
        }

        // JS: SmartObject.removed(virtualWorld?) -> RemovedStation[]
        void JS_Removed(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> rows      = v8::Array::New(isolate);
            std::uint32_t index            = 0;
            for (const auto &[station, virtualWorld] : Objects().Removed(OptionalWorldArg(info, 0))) {
                v8::Local<v8::Object> row = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, FormatLevelGuid(station.guid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, Shared::Game::SmartObjectKindName(station.kind))).Check();
                row->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, station.position)).Check();
                row->Set(context, v8pp::to_v8(isolate, "virtualWorld"), v8pp::to_v8(isolate, static_cast<double>(virtualWorld))).Check();
                rows->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(rows);
        }

        // JS: smartObject.destroy() -> void
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SmartObject *self                           = Self(info);
            Shared::Entities::SmartObjectEntity *object = self != nullptr ? self->ResolveObject() : nullptr;
            if (object != nullptr) {
                Objects().Despawn(*object);
            }
        }

        // JS: smartObject.toJSON() -> SmartObjectDefinition | null
        void JS_ToJSON(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SmartObject *self                                 = Self(info);
            Shared::Entities::SmartObjectEntity *object       = self != nullptr ? self->ResolveObject() : nullptr;
            const Shared::Game::SmartObjectDefinition *placed = object != nullptr ? Objects().DefinitionOf(*object) : nullptr;
            if (placed == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(JsonValue(info.GetIsolate(), Shared::Game::SmartObjectDefinitionToJson(*placed)));
        }
    } // namespace

    Shared::Entities::SmartObjectEntity *SmartObject::ResolveObject() const {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr ? replication->GetEntity<Shared::Entities::SmartObjectEntity>(GetId()) : nullptr;
    }

    std::string SmartObject::GetBlueprint() const {
        const auto *object = ResolveObject();
        const auto *layout = object != nullptr ? object->Blueprint() : nullptr;
        return layout != nullptr ? std::string(layout->name) : std::string();
    }

    std::string SmartObject::GetKind() const {
        const auto *object = ResolveObject();
        const auto *layout = object != nullptr ? object->Blueprint() : nullptr;
        return layout != nullptr ? Shared::Game::SmartObjectKindName(layout->kind) : std::string();
    }

    std::string SmartObject::GetName() const {
        auto *object                                      = ResolveObject();
        const Shared::Game::SmartObjectDefinition *placed = object != nullptr ? Objects().DefinitionOf(*object) : nullptr;
        return placed != nullptr ? placed->name : std::string();
    }

    bool SmartObject::IsBare() const {
        const auto *object = ResolveObject();
        return object != nullptr && object->bare;
    }

    std::string SmartObject::GetStationGuid() const {
        const auto *object = ResolveObject();
        const auto *layout = object != nullptr ? object->Blueprint() : nullptr;
        return layout != nullptr && layout->stationPart >= 0 ? FormatLevelGuid(object->PartGuid(static_cast<std::size_t>(layout->stationPart))) : std::string();
    }

    v8::Local<v8::Value> SmartObject::GetSeats(v8::Isolate *isolate) const {
        nlohmann::json seats = nlohmann::json::array();
        const auto *object   = ResolveObject();
        if (const auto *layout = object != nullptr ? object->Blueprint() : nullptr) {
            for (const std::size_t part : SeatParts(*layout)) {
                seats.push_back(FormatLevelGuid(object->PartGuid(part)));
            }
        }
        return JsonValue(isolate, seats);
    }

    std::string SmartObject::ToString() const {
        std::ostringstream ss;
        ss << "SmartObject{ id: " << GetId() << ", blueprint: " << GetBlueprint() << ", kind: " << GetKind() << " }";
        return ss.str();
    }

    v8::Local<v8::Value> WrapSmartObject(v8::Isolate *isolate, std::uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || networkId == 0 || replication->GetEntity<Shared::Entities::SmartObjectEntity>(networkId) == nullptr) {
            return v8::Null(isolate);
        }
        SmartObject::GetClass(isolate);
        return v8pp::class_<SmartObject>::create_object(isolate, networkId);
    }

    v8pp::class_<SmartObject> &SmartObject::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            Framework::Scripting::Builtins::Entity::GetClass(isolate);
            _class = std::make_unique<v8pp::class_<SmartObject>>(isolate, ServerCatalog(), "SmartObject",
                "A bench, a bed, a dice table, an alchemy table, a smithy, a drying rack or a smokehouse a script or a World Builder export placed. Every player in its virtual world can sit on it, sleep in it or work at it as they would at the level's own. It never moves: assigning `position` or `rotation`, or calling `setVirtualWorld`, is put back on the next tick.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Wraps a smart object the server already holds; use SmartObject.spawn to place one."))
                .function("toString", &SmartObject::ToString, v8pp::metadata::docs("string", {}, "Formats this handle for logging and debugging.", "Its id, blueprint and kind."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Takes the smart object down for every player. A player sitting on it or working at it is let go."));
            _class->prototype_function("toJSON", &JS_ToJSON,
                v8pp::metadata::docs("SmartObjectDefinition | null", {}, "The definition `SmartObject.spawn` takes to place it again: what a script saves. The server keeps nothing past a restart. It is one entry of a World Builder map's `smartObjects` array.",
                    "Null once it is gone."));

            _class->property("blueprint", &SmartObject::GetBlueprint, v8pp::metadata::property_docs("string", "The blueprint it was placed from, as `SmartObject.blueprints()` lists it."));
            _class->property("kind", &SmartObject::GetKind, v8pp::metadata::property_docs("'seat' | 'bed' | 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking'", "What it is for."));
            _class->property("name", &SmartObject::GetName, v8pp::metadata::property_docs("string", "The name it was placed with; empty for none."));
            _class->property("bare", &SmartObject::IsBare, v8pp::metadata::property_docs("boolean", "Whether it was placed without its blueprint's furniture: only the place a body takes, or the station, and the prompts that offer it."));
            _class->property("stationId", &SmartObject::GetStationGuid,
                v8pp::metadata::property_docs("string",
                    "The GUID of its station part as sixteen lowercase hex digits: the `id` `Dice.tables()` and `SmartObject.stations()` give it. `Crafting.stations()` lists an alchemy table or a smithy as `<level>:<stationId>`. Empty for a seat or a bed. It lasts only until the server stops."));
            _class->property("seats", &SmartObject::GetSeats, v8pp::metadata::property_docs("string[]", "The GUIDs of the places a body sits or lies on, as a seated player's stance and `DiceTableInfo.seats` name them."));
        }
        return *_class;
    }

    void SmartObject::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void SmartObject::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &definition = ServerCatalog().data_type("SmartObjectDefinition", "A smart object to place, as `SmartObject.spawn` takes it and a World Builder map's `smartObjects` array stores it.");
        definition.add_property("blueprint", "string", "What to place, by the name `SmartObject.blueprints()` lists: `Bench/bench_2places_low`, `Bed/bed_low`, `Table/table_dice`, `profession/apothecary/alchemyTable_wip`.");
        definition.add_property("position", "Vector3 | number[]", "Where it stands: the prefab's own origin, as the level's copies of it stand.");
        definition.add_property("rotation", v8pp::metadata::type {"Quaternion | number[] | undefined", {}, true}, "Which way it faces: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.");
        definition.add_property("name", v8pp::metadata::type {"string | undefined", {}, true}, "What a script finds it by, up to 64 bytes of UTF-8.");
        definition.add_property("bare", v8pp::metadata::type {"boolean | undefined", {}, true},
            "True to place it without the blueprint's furniture: only the place a body takes, or the station, and the prompts that offer it, for a prop of your own choosing to stand there instead. Only a blueprint `SmartObject.blueprints()` lists with `bare` takes it. False when omitted.");

        auto &blueprint = ServerCatalog().data_type("SmartObjectBlueprintInfo", "Something `SmartObject.spawn` can place.");
        blueprint.add_property("name", "string", "The name `SmartObject.spawn` takes: the game's prefab path under `Prefabs/`.");
        blueprint.add_property("kind", "'seat' | 'bed' | 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking'", "What it is for.");
        blueprint.add_property("seats", "number", "How many bodies can sit or lie on it at once.");
        blueprint.add_property("bare", "boolean", "Whether it can be placed `bare`: false for one whose furniture is one of its own entities, like a chair, or that has none.");

        auto &station = ServerCatalog().data_type("SmartObjectStation", "A station players can use: one the level places, or a placed smart object's.");
        station.add_property("id", "string", "Its GUID as sixteen lowercase hex digits, as `SmartObject.remove` takes it for one of the level's.");
        station.add_property("kind", "'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking'", "What it is for.");
        station.add_property("position", "Vector3", "Where it stands.");
        station.add_property("placed", "boolean", "Whether a script or a World Builder export placed it, rather than the level.");
        station.add_property("smartObject", "SmartObject | null", "The placed smart object it belongs to; null for one of the level's own.");

        auto &removed = ServerCatalog().data_type("RemovedStation", "One of the level's stations a level edit has taken out of a virtual world.");
        removed.add_property("id", "string", "Its GUID, as `SmartObject.restore` takes it.");
        removed.add_property("kind", "'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking'", "What it was for.");
        removed.add_property("position", "Vector3", "Where the level places it.");
        removed.add_property("virtualWorld", "number", "The virtual world it is out of. Out of the global world, it is out of every world.");

        v8pp::class_<SmartObject> &cls = GetClass(isolate);
        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("SmartObject",
                {
                    v8pp::metadata::param("definition", "SmartObjectDefinition", false, "What to place and where, in the format of one entry of a World Builder map's `smartObjects` array."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world whose players see it; the global one when omitted."),
                },
                "Places one of the game's interactive prefabs -- its furniture, solid, and the entities its interaction runs on -- for every player in the virtual world, built on each client under the same GUIDs, so sitting on it and working at it replicate as they do at the level's own. A placed dice table, crafting station, drying rack or smokehouse joins `Dice.tables()`, `Crafting.stations()` and the server's checks like the level's own. Kept in memory only: place it again on boot. The resource that places it owns it, so it goes when that resource stops. NPCs do not use it. A `bare` definition places the entities without the furniture, where a prop of your own stands.",
                "The smart object. Throws for an unknown blueprint, a transform that is not finite, or `bare` on a blueprint that cannot stand bare."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("SmartObject[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the smart objects scripts and World Builder exports placed.",
                "One handle per placed object, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("SmartObject | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a placed smart object up by its network entity ID.", "The handle, or null."));
        cls.static_function("blueprints", &JS_Blueprints, v8pp::metadata::docs("SmartObjectBlueprintInfo[]", {}, "Lists everything `SmartObject.spawn` can place.", "One row per blueprint, sorted by name."));
        cls.static_function("stations", &JS_Stations,
            v8pp::metadata::docs("SmartObjectStation[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list for; the global one when omitted.")},
                "Lists every station players in a virtual world can use: the level's dice tables, crafting stations, drying racks and smokehouses, minus any a level edit took out of that world, and every placed one there or in the global world.",
                "One row per station, in no particular order."));
        cls.static_function("remove", &JS_Remove,
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("id", "string", false, "The level station's GUID, as `SmartObject.stations()` lists it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to take it out of; the global one, and so every world, when omitted."),
                },
                "Takes one of the level's stations out of a virtual world through a level edit on its entity: listed nowhere, refused for any new session by the server (one already running at it finishes), and hidden on every client seeing that world -- a player in the global world sees every world. The furniture around it is the level's own scenery and stays; take it out with `LevelEdit.apply` or the World Builder. A placed smart object goes with `destroy()` instead. Kept in memory only, like every level edit.",
                "Throws for an id that is not one of the level's stations."));
        cls.static_function("restore", &JS_Restore,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("id", "string", false, "The level station's GUID, as `SmartObject.removed()` lists it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to put it back in; the global one when omitted."),
                },
                "Puts back one of the level's stations that a level edit took out of a virtual world.", "False when that world had no such removal."));
        cls.static_function("removed", &JS_Removed,
            v8pp::metadata::docs("RemovedStation[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the level's stations a level edit has taken out, by a script or a World Builder export alike.",
                "One row per removal, in no particular order."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
