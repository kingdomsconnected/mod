#include "world_scripting.h"

#include "core/scripting/script_context.h"
#include "core/server.h"

#include "features/world/world_query_scripting.h"

#include "shared/features/world/world_entity.h"
#include "shared/game/poi_catalog.h"
#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        void PointsOfInterest(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::string level = Server::Get()->GetModConfig().Get<std::string>("level");
            nlohmann::json rows = nlohmann::json::array();
            for (const auto &point : Shared::Game::g_PointsOfInterest) {
                if (point.level != level) {
                    continue;
                }
                rows.push_back({{"name", point.name}, {"group", point.group}, {"level", point.level}, {"position", {{"x", point.x}, {"y", point.y}, {"z", point.z}}}});
            }
            v8::Local<v8::Value> result;
            if (v8::JSON::Parse(info.GetIsolate()->GetCurrentContext(), v8pp::to_v8(info.GetIsolate(), rows.dump()).As<v8::String>()).ToLocal(&result)) {
                info.GetReturnValue().Set(result);
            }
        }

        // Unguarded on purpose: PostInit installs the world and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::World::WorldService &ResolveWorld() {
            return Server::Get()->GetWorld();
        }

        // A finite number in that slot, or `fallback` when it is empty. Present but not a number fails rather than falling back.
        bool ReadNumber(const v8::FunctionCallbackInfo<v8::Value> &info, int index, double fallback, double &out) {
            if (info.Length() <= index) {
                out = fallback;
                return true;
            }

            if (!info[index]->IsNumber()) {
                return false;
            }

            out = v8pp::from_v8<double>(info.GetIsolate(), info[index]);

            return std::isfinite(out);
        }
    } // namespace

    void World::JS_SetTime(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        double day  = 0;
        double hour = 0;
        if (info.Length() < 2 || !ReadNumber(info, 0, 0, day) || !ReadNumber(info, 1, 0, hour)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setTime: expected (day, hour)"));
            return;
        }

        // Refused rather than truncated: half a day is a typo, not a moment.
        if (day < 0 || day > static_cast<double>(Shared::Entities::WorldEntity::kMaxDay) || day != std::floor(day)) {
            info.GetReturnValue().Set(false);
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetTime(static_cast<std::int64_t>(day), hour));
    }

    void World::JS_SetHour(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        double hour = 0;
        if (info.Length() < 1 || !ReadNumber(info, 0, 0, hour)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setHour: expected (hour)"));
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetHour(hour));
    }

    void World::JS_SetTimeScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        double scale = 0;
        if (info.Length() < 1 || !ReadNumber(info, 0, 0, scale)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setTimeScale: expected (scale)"));
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetTimeScale(static_cast<float>(scale)));
    }

    void World::JS_SetWeather(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        double seconds = 0;
        if (info.Length() < 1 || !info[0]->IsString() || !ReadNumber(info, 1, 0, seconds)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setWeather: expected (preset, seconds?)"));
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetWeather(v8pp::from_v8<std::string>(isolate, info[0]), seconds));
    }

    void World::JS_SetRain(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        double intensity = 0;
        double amount    = 1;
        if (info.Length() < 1 || !ReadNumber(info, 0, 0, intensity) || !ReadNumber(info, 1, 1, amount)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setRain: expected (intensity, amount?)"));
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetRain(static_cast<float>(intensity), static_cast<float>(amount)));
    }

    void World::JS_SetWind(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        // Guarded, not caught: the converter throws on a non-object and this
        // is a raw callback with no wrapper.
        if (info.Length() < 1 || !info[0]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.setWind: expected (wind)"));
            return;
        }

        info.GetReturnValue().Set(ResolveWorld().SetWind(v8pp::convert<glm::vec3>::from_v8(isolate, info[0])));
    }

    void World::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("worldDayChange", "[day: number]", "Dispatched when the world clock crosses midnight, whether it walked over or `World.setTime` jumped past. The day it carries is the one that just began.");
        events.add_property("worldWeatherChange", "[preset: string, previous: string, seconds: number]",
            "Dispatched when the sky starts blending to another time-of-day preset, from `World.setWeather` or the `/world weather` command. It is raised when the blend starts, not when it settles.");

        v8pp::module world(isolate, ServerCatalog(), "World", "The server's own clock and weather, which every client follows, and the three questions it can ask a client's engine about the level itself.");

        world.property("level", []() { return Server::Get()->GetModConfig().Get<std::string>("level"); }, v8pp::metadata::property_docs("string", "The configured level loaded by every client in this server session."));
        world.function("pointsOfInterest", &PointsOfInterest, v8pp::metadata::docs("{ name: string; group: string; level: string; position: { x: number; y: number; z: number } }[]", {}, "Points of interest in the configured level only, from the compiled game catalog. Positions are ground-marker clusters checked against the level navmesh; a teleport still needs the destination to stream on the client."));

        world.property("day", []() { return ResolveWorld().GetDay(); },
            v8pp::metadata::property_docs("number", "Whole days the clock has run, from the level's own midnight. Starts at 0 and only grows."));
        world.property("hour", []() { return ResolveWorld().GetHour(); },
            v8pp::metadata::property_docs("number", "Hour within the current day, from 0 up to but not including 24, with the minutes as the fraction. 13.5 is half past one."));
        world.property("timeScale", []() { return ResolveWorld().GetTimeScale(); },
            v8pp::metadata::property_docs("number", "How many game seconds pass per real second. 15 is the game's own pace, 0 stops the clock, and the server drops it to 0 by itself at the clock's ceiling."));
        world.property("weather", []() { return ResolveWorld().GetWeather(); },
            v8pp::metadata::property_docs("string", "The time-of-day preset the sky is blending towards, which is where it settles. Set it with `setWeather`."));
        world.property("previousWeather", []() { return ResolveWorld().GetPreviousWeather(); },
            v8pp::metadata::property_docs("string", "The preset the sky is blending from; the same as `weather` once it has settled."));
        world.property("weatherBlending", []() { return ResolveWorld().IsWeatherBlending(); },
            v8pp::metadata::property_docs("boolean", "Whether a blend is still running. `setWeather` is refused while it is: a half-finished blend has no single preset to leave from."));
        world.property("weatherRemaining", []() { return ResolveWorld().GetWeatherRemaining(); },
            v8pp::metadata::property_docs("number", "Game seconds left of the blend, and 0 once the sky has settled. `setWeather` is refused until then."));
        world.property("rainIntensity", []() { return ResolveWorld().GetRainIntensity(); }, v8pp::metadata::property_docs("number", "How hard it is raining, from 0 to 1."));
        world.property("rainAmount", []() { return ResolveWorld().GetRainAmount(); },
            v8pp::metadata::property_docs("number", "How much of the rain is drawn, from 0 to 1. Thins the downpour without stopping it."));
        world.property("wind", []() { return ResolveWorld().GetWind(); },
            v8pp::metadata::property_docs("Vector3", "The wind every client is blowing, in metres per second, world space. It bends the trees, drags the cloth and slants the rain, and it is the one part of the weather the game itself never touches. Set it with `setWind`."));
        world.property("windSpeed", []() { return ResolveWorld().GetWindSpeed(); }, v8pp::metadata::property_docs("number", "How hard the wind is blowing, whichever way: the length of `wind`, from 0 to 50."));
        world.property("wetness", []() { return ResolveWorld().GetWetness(); },
            v8pp::metadata::property_docs("number", "How wet the ground has become, from 0 to 3. Derived from how long it has been raining, so it is read-only, and it dries far slower than it soaks."));
        world.property("puddles", []() { return ResolveWorld().GetPuddles(); },
            v8pp::metadata::property_docs("number", "Puddle coverage, from 0 to 1. Derived like `wetness`, and only starts filling after the rain has run a while."));

        world.function("setTime", &World::JS_SetTime,
            v8pp::metadata::docs("boolean", {
                v8pp::metadata::param("day", "number", false, "Absolute day to wind forward to, as the `day` property counts them. Must be whole."),
                v8pp::metadata::param("hour", "number", false, "Hour of that day, from 0 up to but not including 24."),
            }, "Winds the clock forward to an absolute day and hour, which is how a saved clock is restored.",
                "True when the clock moved; false when that moment is already past, or the day or hour is out of range."));
        world.function("setHour", &World::JS_SetHour,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("hour", "number", false, "Hour to wind forward to, from 0 up to but not including 24.")},
                "Winds the clock forward to the next time it is this hour, rolling into tomorrow when it has passed today. The clock never runs backwards: clients cannot be wound back with it.",
                "True when the clock moved; false when the hour is out of range."));
        world.function("setTimeScale", &World::JS_SetTimeScale,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("scale", "number", false, "Game seconds per real second, from 0 to 200. 0 freezes the clock where it stands.")},
                "Sets how fast the day passes for everyone.", "True when the scale was taken; false when it is out of range."));
        world.function("setWeather", &World::JS_SetWeather,
            v8pp::metadata::docs("boolean", {
                v8pp::metadata::param("preset", "string", false, "Name of one of the game's own time-of-day presets, such as `cloudless_sunny`."),
                v8pp::metadata::param("seconds", "number", true, "Game seconds to blend over, up to 21600. Omitted, the sky changes at once."),
            }, "Blends the sky to another of the game's time-of-day presets, and raises `worldWeatherChange`. Only a client can tell whether a preset exists, so a name the game does not know leaves every sky where it is.",
                "True when the blend started; false while an earlier one is still running, or when the name or duration is out of range."));
        world.function("setRain", &World::JS_SetRain,
            v8pp::metadata::docs("boolean", {
                v8pp::metadata::param("intensity", "number", false, "How hard it rains, from 0 to 1. 0 stops it."),
                v8pp::metadata::param("amount", "number", true, "How much of the rain is drawn, from 0 to 1; defaults to all of it."),
            }, "Sets the rain, which is separate from the sky preset: a preset can be overcast without a drop falling. Starting or stopping it restarts the phase the ground soaks and dries over.",
                "True when the rain was taken; false when either value is out of range."));

        world.function("setWind", &World::JS_SetWind,
            v8pp::metadata::docs("boolean", {
                v8pp::metadata::param("wind", "Vector3 | Partial<Vector3>", false, "Wind velocity in metres per second, world space; omitted components are zero. Its length may not exceed 50."),
            }, "Sets the wind for everyone. There is nothing to blend against -- the engine takes a vector and holds it -- so a gust is a script ramping this itself. It also drives a physical wind area, which is why the length is capped.",
                "True when the wind was taken; false when a component is not finite or it blows harder than 50 m/s."));

        // Appended rather than published separately: a global can only be
        // published once, and `World` is where a resource will look.
        RegisterWorldQueryBindings(isolate, world);

        world.publish(global);
    }

    void Events::WorldDayChanged(std::int64_t day) {
        EmitReservedEvent("worldDayChange", [day](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, day));
            return args;
        });
    }

    void Events::WorldWeatherChanged(const std::string &preset, const std::string &previous, double seconds) {
        EmitReservedEvent("worldWeatherChange", [&preset, &previous, seconds](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, preset));
            args.push_back(v8pp::to_v8(isolate, previous));
            args.push_back(v8pp::to_v8(isolate, seconds));
            return args;
        });
    }
} // namespace KCDC::Scripting
