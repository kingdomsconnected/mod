#include "patrol_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "features/npc/npc_verbs.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {

    namespace {
        v8::Local<v8::Value> JsonValue(v8::Isolate *isolate, const nlohmann::json &value) {
            return ParseJsonValue(isolate, value.dump(), v8::Null(isolate));
        }
        void Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            std::string error = "PatrolRoute.create expects a definition";
            Shared::Game::PatrolRouteDefinition route;
            const auto text = info.Length() ? StringifyValue(isolate, info[0]) : std::nullopt;
            if (!text || !Shared::Game::PatrolRouteFromJson(nlohmann::json::parse(*text, nullptr, false), route, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, error));
                return;
            }
            try {
                info.GetReturnValue().Set(WrapPatrolRoute(isolate, Features::Patrol::PatrolRoutes::Get().Create(route)));
            }
            catch (const std::exception &exception) {
                isolate->ThrowError(v8pp::to_v8(isolate, exception.what()));
            }
        }
        void Update(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate    = info.GetIsolate();
            const auto *self = v8pp::class_<PatrolRoute>::unwrap_object(isolate, info.This());
            if (!self || !self->Resolve()) {
                info.GetReturnValue().Set(false);
                return;
            }
            const auto text = info.Length() ? StringifyValue(isolate, info[0]) : std::nullopt;
            auto patch      = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
            if (!patch.is_object() || (patch.contains("id") && patch["id"] != self->Id())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "PatrolRoute.update expects changes; the id cannot change"));
                return;
            }
            if (!self->Resolve()) {
                info.GetReturnValue().Set(false);
                return;
            }
            auto json = Shared::Game::PatrolRouteToJson(*self->Resolve(), true);
            json.update(patch);
            Shared::Game::PatrolRouteDefinition route;
            std::string error;
            if (!Shared::Game::PatrolRouteFromJson(json, route, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, error));
                return;
            }
            info.GetReturnValue().Set(Features::Patrol::PatrolRoutes::Get().Update(self->Handle(), route));
        }
        void Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (!info.Length() || !info[0]->IsString()) {
                info.GetIsolate()->ThrowError("PatrolRoute.getById expects an id");
                return;
            }
            info.GetReturnValue().Set(WrapPatrolRoute(info.GetIsolate(), Features::Patrol::PatrolRoutes::Get().Find(v8pp::from_v8<std::string>(info.GetIsolate(), info[0]))));
        }
        void All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate       = info.GetIsolate();
            auto array          = v8::Array::New(isolate);
            std::uint32_t index = 0;
            for (const auto &[handle, route] : Features::Patrol::PatrolRoutes::Get().All()) {
                array->Set(isolate->GetCurrentContext(), index++, WrapPatrolRoute(isolate, handle)).Check();
            }
            info.GetReturnValue().Set(array);
        }
    } // namespace

    v8::Local<v8::Value> WrapPatrolRoute(v8::Isolate *isolate, std::uint32_t handle) {
        if (!Features::Patrol::PatrolRoutes::Get().Find(handle)) {
            return v8::Null(isolate);
        }
        return v8pp::class_<PatrolRoute>::create_object(isolate, handle);
    }

    v8::Local<v8::Value> PatrolRoute::ToJSON(v8::Isolate *isolate) const {
        const auto *route = Resolve();
        return JsonValue(isolate, route ? Shared::Game::PatrolRouteToJson(*route, true) : nlohmann::json(nullptr));
    }

    bool ReadPatrolRoute(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::PatrolRouteDefinition &route, std::string &error) {
        const Shared::Game::PatrolRouteDefinition *found = nullptr;
        if (value->IsString()) {
            found = Features::Patrol::PatrolRoutes::Get().Find(Features::Patrol::PatrolRoutes::Get().Find(v8pp::from_v8<std::string>(isolate, value)));
        }
        else if (value->IsObject()) {
            const auto *handle = v8pp::class_<PatrolRoute>::unwrap_object(isolate, value);
            if (handle) {
                found = handle->Resolve();
            }
        }
        if (!found) {
            error = "Unknown or destroyed patrol route; expected a PatrolRoute or its id";
            return false;
        }
        route = *found;
        return true;
    }

    bool ApplyPatrolOptions(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::PatrolRouteDefinition &route, std::string &error) {
        if (value.IsEmpty() || value->IsNullOrUndefined()) {
            return true;
        }
        const auto text    = StringifyValue(isolate, value);
        const auto options = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
        error              = "Invalid patrol options";
        if (!options.is_object()) {
            return false;
        }
        auto json = Shared::Game::PatrolRouteToJson(route);
        if (options.contains("loop")) {
            if (!options["loop"].is_boolean() || options.contains("mode")) {
                return false;
            }
            json["mode"] = options["loop"].get<bool>() ? "loop" : "once";
        }
        for (const char *key : {"mode", "speed", "waitSeconds", "radius"}) {
            if (options.contains(key)) {
                json[key] = options[key];
            }
        }
        return Shared::Game::PatrolRouteFromJson(json, route, error, !route.id.empty());
    }

    void EmitPatrolEvents(std::uint64_t networkId, std::vector<nlohmann::json> events) {
        for (auto &event : events) {
            if (!Features::Npc::Npcs::Find(networkId)) {
                break;
            }
            const bool finished = event.at("finished").get<bool>();
            event.erase("finished");
            EmitReservedEvent(finished ? "patrolFinished" : "patrolWaypoint", [networkId, event](v8::Isolate *isolate) { return std::vector<v8::Local<v8::Value>> {WrapNpc(isolate, networkId), JsonValue(isolate, event)}; });
        }
    }

    void PatrolRoute::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &point = ServerCatalog().data_type("PatrolPoint", "One authored stop, independent of actor type.");
        point.add_property("position", "Vector3", "World-space waypoint.");
        point.add_property("speed", v8pp::metadata::type {"'walk' | 'jog' | 'run' | undefined", {}, true}, "Overrides the route and call speed for this leg.");
        point.add_property("waitSeconds", v8pp::metadata::type {"number | undefined", {}, true}, "Wait after arrival, 0 to 3600 seconds.");
        point.add_property("radius", v8pp::metadata::type {"number | undefined", {}, true}, "Arrival radius in metres.");
        auto &definition = ServerCatalog().data_type("PatrolRouteDefinition", "The format saved by World Builder and accepted by PatrolRoute.create.");
        definition.add_property("id", "string", "Stable globally unique id, up to 128 bytes. Use a namespace such as town.guard-loop.");
        definition.add_property("name", v8pp::metadata::type {"string | undefined", {}, true}, "Display name, up to 256 bytes.");
        definition.add_property("points", "PatrolPoint[]", "1 to 256 ordered waypoints.");
        definition.add_property("mode", v8pp::metadata::type {"'once' | 'loop' | 'pingPong' | undefined", {}, true}, "Defaults to loop. Ping-pong visits each endpoint once per turn.");
        definition.add_property("speed", v8pp::metadata::type {"'walk' | 'jog' | 'run' | undefined", {}, true}, "Default leg speed; walk when omitted.");
        definition.add_property("waitSeconds", v8pp::metadata::type {"number | undefined", {}, true}, "Default arrival wait, 0 to 3600; defaults to zero.");
        definition.add_property("radius", v8pp::metadata::type {"number | undefined", {}, true}, "Default arrival radius; 1.5 metres when omitted.");
        auto &state = ServerCatalog().data_type("PatrolState", "A running patrol's snapshot. Indices and laps start at zero.");
        state.add_property("routeId", "string | null", "Stable route id; null for an anonymous point-array patrol.");
        state.add_property("waypointIndex", "number", "Target waypoint index.");
        state.add_property("lap", "number", "Completed loops or round trips.");
        state.add_property("direction", "number", "1 forward, -1 backward.");
        state.add_property("phase", "'moving' | 'waiting'", "Whether the actor is approaching or waiting at the waypoint.");
        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("patrolWaypoint", "[actor: Npc, info: PatrolState & { status: 'reached' | 'blocked' | 'failed' }]", "One accepted waypoint outcome; intermediate path corners do not emit it.");
        events.add_property("patrolFinished", "[actor: Npc, info: PatrolState & { status: 'completed' | 'failed' | 'cancelled' }]", "A patrol ended, failed, or was replaced. Edits to its definition do not replace its snapshot.");
        v8pp::class_<PatrolRoute> cls(isolate, ServerCatalog(), "PatrolRoute", "Named patrol definition. Actors snapshot it when ordered; update or destroy affects future starts.");
        cls.auto_wrap_objects(true);
        cls.property("exists", &PatrolRoute::Exists, v8pp::metadata::property_docs("boolean", "False after destruction."));
        cls.property("id", &PatrolRoute::Id, v8pp::metadata::property_docs("string", "Stable id; empty after destruction."));
        cls.property("name", &PatrolRoute::Name, v8pp::metadata::property_docs("string", "Display name."));
        cls.function("toJSON", &PatrolRoute::ToJSON, v8pp::metadata::docs("PatrolRouteDefinition | null", {}, "Returns an independent definition, or null after destruction."));
        cls.function("destroy", &PatrolRoute::Destroy, v8pp::metadata::docs("boolean", {}, "Removes the definition. Active patrols keep their snapshots."));
        cls.prototype_function("update", Update,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("changes", "Partial<PatrolRouteDefinition>", false, "Fields to replace; id cannot change.")}, "Updates defaults or waypoints atomically. Invalid input throws; a destroyed handle returns false."));
        cls.static_function("create", Create, v8pp::metadata::docs("PatrolRoute", {v8pp::metadata::param("definition", "PatrolRouteDefinition", false, "The route to register.")}, "Registers a route. Invalid data or a duplicate id throws."));
        cls.static_function("getById", Find, v8pp::metadata::docs("PatrolRoute | null", {v8pp::metadata::param("id", "string", false, "Exact route id.")}, "Looks up a registered route."));
        cls.static_function("all", All, v8pp::metadata::docs("PatrolRoute[]", {}, "Lists registered routes in creation order."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
