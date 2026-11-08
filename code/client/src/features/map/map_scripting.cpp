#include "map_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"

#include "features/map/map_service.h"

#include "common/vector_math.h"

#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        KCDC::Features::Map::MapService *Map() {
            return Core::gApplication ? &Core::gApplication->GetMapService() : nullptr;
        }

        bool IsMapOpen() {
            KCDC::Features::Map::MapService *map = Map();
            return map != nullptr && map->IsMapOpen();
        }

        double CompassMarkCount() {
            return Core::gApplication ? static_cast<double>(Core::gApplication->GetMapBlipService().GetCompassMarkCount()) : 0.0;
        }

        bool IsCompassVisible() {
            KCDC::Features::Map::MapService *map = Map();
            return map == nullptr || map->IsCompassVisible();
        }

        void SetCompassVisible(bool visible) {
            if (KCDC::Features::Map::MapService *map = Map()) {
                map->SetCompassVisible(visible);
            }
        }

        bool IsTraveling() {
            KCDC::Features::Map::MapService *map = Map();
            return map != nullptr && map->IsTraveling();
        }

        glm::vec3 TravelPosition() {
            KCDC::Features::Map::MapService *map = Map();
            return map != nullptr ? map->GetTravelPosition() : glm::vec3 {0.0F};
        }

        // Reads a mark type argument, throwing on a name the game does not know.
        bool ReadMarkType(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *usage, std::string &type) {
            v8::Isolate *isolate = info.GetIsolate();

            if (info.Length() <= index || !info[index]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage)));
                return false;
            }

            type = v8pp::from_v8<std::string>(isolate, info[index]);
            if (!KCDC::Features::Map::MapService::IsKnownMarkType(type)) {
                isolate->ThrowError(v8pp::to_v8(isolate, type + " is not one of the game's compass mark types"));
                return false;
            }
            return true;
        }

        // A LocationId as `poiDiscovered` hands it out: three numbers and eight bytes.
        bool ReadLocationId(v8::Isolate *isolate, v8::Local<v8::Value> value, std::uint32_t &data1, std::uint16_t &data2, std::uint16_t &data3, std::uint8_t (&data4)[8]) {
            if (!value->IsObject()) {
                return false;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> id       = value.As<v8::Object>();
            double fields[3]               = {};
            const double limits[3]         = {4294967295.0, 65535.0, 65535.0};
            const char *names[3]           = {"data1", "data2", "data3"};
            for (int i = 0; i < 3; ++i) {
                v8::Local<v8::Value> field;
                if (!id->Get(context, v8pp::to_v8(isolate, names[i])).ToLocal(&field) || !field->IsNumber()) {
                    return false;
                }
                fields[i] = v8pp::from_v8<double>(isolate, field);
                if (!(fields[i] >= 0.0 && fields[i] <= limits[i]) || fields[i] != std::floor(fields[i])) {
                    return false;
                }
            }
            v8::Local<v8::Value> tail;
            if (!id->Get(context, v8pp::to_v8(isolate, "data4")).ToLocal(&tail) || !tail->IsUint8Array() || tail.As<v8::Uint8Array>()->Length() != sizeof(data4)) {
                return false;
            }
            tail.As<v8::Uint8Array>()->CopyContents(data4, sizeof(data4));
            data1 = static_cast<std::uint32_t>(fields[0]);
            data2 = static_cast<std::uint16_t>(fields[1]);
            data3 = static_cast<std::uint16_t>(fields[2]);
            return true;
        }

        // Both waypoint events: where it was, and on which map.
        void EmitWaypointEvent(const char *eventName, const glm::vec3 &position, std::int32_t mapId, const bool *moved) {
            EmitReservedEvent(eventName, [&position, mapId, moved](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(v8pp::convert<glm::vec3>::to_v8(isolate, position));
                args.push_back(v8pp::to_v8(isolate, static_cast<double>(mapId)));
                if (moved != nullptr) {
                    args.push_back(v8pp::to_v8(isolate, *moved));
                }
                return args;
            });
        }
    } // namespace

    void WorldMap::JS_GetWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Map::MapService *map = Map();
        KCDC::Features::Map::MapService::Waypoint placed;
        if (map == nullptr || !map->GetWaypoint(placed)) {
            info.GetReturnValue().SetNull();
            return;
        }

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> waypoint = v8::Object::New(isolate);
        waypoint->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, placed.position)).Check();
        waypoint->Set(context, v8pp::to_v8(isolate, "mapId"), v8pp::to_v8(isolate, static_cast<double>(placed.mapId))).Check();

        info.GetReturnValue().Set(waypoint);
    }

    void WorldMap::JS_MoveWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.moveWaypoint: expected (position)"));
            return;
        }

        const glm::vec3 requested = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
        if (!KCDC::Common::IsFinite(requested)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.moveWaypoint: position must be finite"));
            return;
        }

        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->MoveWaypoint(requested));
    }

    void WorldMap::JS_ClearWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->ClearWaypoint());
    }

    void WorldMap::JS_IsCategoryVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        std::string type;
        if (!ReadMarkType(info, 0, "WorldMap.isCategoryVisible: expected (type)", type)) {
            return;
        }

        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->IsMarkTypeVisible(type));
    }

    void WorldMap::JS_SetCategoryVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        std::string type;
        if (!ReadMarkType(info, 0, "WorldMap.setCategoryVisible: expected (type, visible)", type)) {
            return;
        }

        if (info.Length() < 2 || !info[1]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.setCategoryVisible: expected (type, visible)"));
            return;
        }

        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->SetMarkTypeVisible(type, v8pp::from_v8<bool>(isolate, info[1])));
    }

    void WorldMap::JS_CanStartTravel(const v8::FunctionCallbackInfo<v8::Value> &info) {
        KCDC::Features::Map::MapService *map         = Map();
        const std::optional<std::uint8_t> refusalCode = map != nullptr ? map->ValidateTravelStart() : std::nullopt;
        if (!refusalCode.has_value()) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(static_cast<double>(*refusalCode));
    }

    void WorldMap::JS_Discover(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        std::uint32_t data1 = 0;
        std::uint16_t data2 = 0;
        std::uint16_t data3 = 0;
        std::uint8_t data4[8] {};
        if (info.Length() < 1 || !ReadLocationId(isolate, info[0], data1, data2, data3, data4)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.discover: expected (id: LocationId)"));
            return;
        }

        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->Discover(data1, data2, data3, data4));
    }

    void WorldMap::JS_RevealFog(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.revealFog: expected (position)"));
            return;
        }

        const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
        if (!KCDC::Common::IsFinite(position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "WorldMap.revealFog: position must be finite"));
            return;
        }

        KCDC::Features::Map::MapService *map = Map();
        info.GetReturnValue().Set(map != nullptr && map->RevealFog(position));
    }

    void WorldMap::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("mapOpened", "[]", "Dispatched when the map screen comes up at this machine.");
        events.add_property("mapClosed", "[]", "Dispatched when the map screen goes away.");
        events.add_property("mapWaypointSet", "[position: Vector3, mapId: number, moved: boolean]",
            "Dispatched when the player drops their own marker on the map, or moves the one already down -- `moved` separates the two, because the game uses one action for both. The position is world-space metres. Maps reuse one coordinate range, which is why `mapId` travels with it.");
        events.add_property("mapWaypointCleared", "[position: Vector3, mapId: number]", "Dispatched when the player takes their marker off the map, including through `WorldMap.clearWaypoint`. It carries where the marker was, since afterwards nothing says.");

        auto &waypoint = ClientCatalog().data_type("MapWaypoint", "The marker the player has dropped on the map, as `WorldMap.getWaypoint` reports it.");
        waypoint.add_property("position", "Vector3", "Where the marker sits, in world-space metres. Its z is the terrain height under the point.");
        waypoint.add_property("mapId", "number", "Which map it belongs to. The maps reuse one coordinate range.");

        v8pp::module map(isolate, ClientCatalog(), "WorldMap", "The map screen at this machine, and the player's own marker on it. Nothing here is replicated: a marker everyone should see is one the server tells every client to make.");

        map.property("isOpen", &IsMapOpen, v8pp::metadata::property_docs("boolean", "Whether the map screen is up right now. False in the main menu and across a level load, when there is no map page at all."));

        map.function("getWaypoint", &WorldMap::JS_GetWaypoint,
            v8pp::metadata::docs("MapWaypoint | null", {}, "Reads the marker the player has dropped on the map they are looking at. The game keeps at most one per map.", "The marker's position and the map it belongs to, or null when there is none."));
        map.function("moveWaypoint", &WorldMap::JS_MoveWaypoint,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("position", "Vector3", false, "Where to put it, in world-space metres. Nothing snaps it to the terrain.")},
                "Moves the marker the player has already dropped. It cannot create one, so it reads false when there is nothing down.", "True when there was a marker to move."));
        map.function("clearWaypoint", &WorldMap::JS_ClearWaypoint,
            v8pp::metadata::docs("boolean", {}, "Takes the player's marker off the map they are looking at. It runs the game's own removal, so `mapWaypointCleared` is raised as it would be for the player.", "True when there was a marker to take off."));

        map.function("isCategoryVisible", &WorldMap::JS_IsCategoryVisible,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("type", "string", false, "One of the game's own compass mark types, spelled the way the game spells it -- `Shop`, `QuestGiver`, `GeneralPoi`. The full list is the `type` a `Blip` accepts.")},
                "Whether marks of this type are currently drawn on the compass and listed in the map legend.", "True when they are shown."));
        map.function("setCategoryVisible", &WorldMap::JS_SetCategoryVisible,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("type", "string", false, "One of the game's own compass mark types, spelled the way the game spells it -- `Shop`, `QuestGiver`, `GeneralPoi`. The full list is the `type` a `Blip` accepts."),
                    v8pp::metadata::param("visible", "boolean", false, "Whether marks of that type should be drawn."),
                },
                "Shows or hides a whole category of the game's own map markers -- every shop, every quest giver -- through the switch the map legend's rows drive. It affects the marks already placed as well as later ones. DLC icons share a legend row with the entry they belong to, so setting one sets the group.",
                "True when the map page was there to take it."));

        map.function("discover", &WorldMap::JS_Discover,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("id", "LocationId", false, "A point of interest or a location -- a town, a district -- as `poiDiscovered` or `locationDiscovered` handed it out. `data4` must be a Uint8Array of eight bytes.")},
                "Marks a place on this player's map as discovered, silently: no toast, no XP, no discovery trigger. What a resource that keeps a character's discoveries calls after the character is loaded, since every session starts on a fresh map. A point of interest raises `poiDiscovered` as it is marked; a location raises nothing.",
                "True when the id named a place in the loaded level; false otherwise, or before the level is up."));
        map.function("revealFog", &WorldMap::JS_RevealFog,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("position", "Vector3", false, "A world-space point; only x and y count.")},
                "Clears the fog of war around a point by the game's own reveal radius, as walking there does. What a resource that keeps where a character has been calls after the character is loaded. Only matters while the server's `reveal_map` is off; otherwise there is no fog.",
                "True when the level's fog took it; false before the level is up."));
        map.function(
            "revealAllFog",
            []() {
                KCDC::Features::Map::MapService *map = Map();
                return map != nullptr && map->RevealAllFog();
            },
            v8pp::metadata::docs("boolean", {},
                "Clears the fog of war over the whole level, as the game's own `player_revealFow` does, and leaves discovery alone: places still become discovered when the player finds them. The fog is also what hides the names of places not yet found, so from then on the map writes a town's name only once the player knows of it. What a server that keeps `reveal_map` off for the discoveries, but wants no fog, calls once the character is loaded. A level load brings the fog back.",
                "True when the level's fog took it; false before the level is up."));

        map.publish(global);

        v8pp::module compass(isolate, ClientCatalog(), "Compass", "The compass strip along the top of the HUD, which is what this game has instead of a minimap. Marks go on it through `Blip`; this is the strip itself.");

        compass.property("markCount", &CompassMarkCount,
            v8pp::metadata::property_docs("number", "How many marks the compass carried at the last tick, the game's own included. Sampled rather than read live: the vector it comes from is reallocated by the thread that adds to it."));
        compass.property("visible", &IsCompassVisible, &SetCompassVisible);
        compass.document_property("visible",
            v8pp::metadata::property_docs("boolean",
                "Whether the compass is showing. Assignment pushes the game's own suppression, the one a cutscene uses, so it takes the whole strip rather than emptying it. It reads the mod's own view -- a suppression the game pushed is invisible here -- and the mod's is released when the session ends."));

        compass.publish(global);

        v8pp::module travel(isolate, ClientCatalog(), "Travel",
            "The local player's fast travel, as something to read. There is deliberately no verb: starting a travel moves the player, advances the world clock and can fire a random event, so in a session that is the server's decision rather than one for a resource on the traveller's own machine.");

        travel.property("active", &IsTraveling, v8pp::metadata::property_docs("boolean", "Whether a fast travel is running right now."));
        travel.property("position", &TravelPosition, v8pp::metadata::property_docs("Vector3", "Where the travel has got to along its route, in world-space metres. Zero while none is running: check `active` first."));
        travel.function("canStart", &WorldMap::JS_CanStartTravel,
            v8pp::metadata::docs("number | null", {}, "Asks the game whether a fast travel could begin right now, without starting one. The answer is the game's own refusal code; only 0 is proven, and it means yes.", "The refusal code, or null before the player module is up."));

        travel.publish(global);
    }

    void Events::MapOpened() {
        EmitReservedEvent("mapOpened", [](v8::Isolate *) {
            return std::vector<v8::Local<v8::Value>> {};
        });
    }

    void Events::MapClosed() {
        EmitReservedEvent("mapClosed", [](v8::Isolate *) {
            return std::vector<v8::Local<v8::Value>> {};
        });
    }

    void Events::MapWaypointSet(const glm::vec3 &position, std::int32_t mapId, bool moved) {
        EmitWaypointEvent("mapWaypointSet", position, mapId, &moved);
    }

    void Events::MapWaypointCleared(const glm::vec3 &position, std::int32_t mapId) {
        EmitWaypointEvent("mapWaypointCleared", position, mapId, nullptr);
    }
} // namespace KCDC::Scripting
