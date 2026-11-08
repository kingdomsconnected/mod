#include "navigation_scripting.h"
#include "features/door/door_service.h"

#include "core/server.h"

#include "features/horse/horse_scripting.h"
#include "features/navigation/navigation_service.h"
#include "features/npc/npc_order.h"
#include "features/npc/npc_scripting.h"
#include "features/patrol/patrol_scripting.h"
#include "features/patrol/patrol_validation.h"

#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** How far a query may be told to look for the mesh; past this it is no longer a nearby point. */
        constexpr double kMaxSearchRadius = 64.0;
        constexpr double kMaxSearchHeight = 128.0;

        /** How far `randomPoint` may reach; a wider circle is a level-wide search. */
        constexpr double kMaxRandomRadius = 512.0;

        /** The options every query takes. Inline rather than a named type, so each field stays optional in the contract. */
        constexpr const char *kQueryOptionsType = "{ searchRadius?: number; searchHeight?: number; doors?: \"unlocked\" | \"open\" | \"any\" | \"none\" }";

        // Unguarded on purpose: PostInit installs the service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::Navigation::NavigationService &ResolveNavigation() {
            return Server::Get()->GetNavigation();
        }

        void Throw(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowError(v8pp::to_v8(isolate, message));
        }

        /** A finite world-space point in that slot, or false. */
        bool ReadPoint(const v8::FunctionCallbackInfo<v8::Value> &info, int index, glm::vec3 &point) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return false;
            }
            point = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        /** A field the options object in that slot gives, or an empty handle when there is no object or the field is undefined. Null counts as given, and is refused as the wrong type. */
        v8::Local<v8::Value> GivenField(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::Local<v8::Value> value;
            if (info.Length() <= index || !info[index]->IsObject() || !info[index].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) || value->IsUndefined()) {
                return v8::Local<v8::Value>();
            }
            return value;
        }

        /** A search distance the options give, which must be a number in (0, max]; false, having thrown, when it is not. */
        bool ReadDistance(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key, const char *verb, double max, double &out) {
            const v8::Local<v8::Value> value = GivenField(info, index, key);
            if (value.IsEmpty()) {
                return true;
            }
            out = value->IsNumber() ? v8pp::from_v8<double>(info.GetIsolate(), value) : 0.0;
            if (!(out > 0.0 && out <= max)) {
                Throw(info.GetIsolate(), std::string(verb) + ": " + key + " must be a number in (0, " + std::to_string(static_cast<int>(max)) + "]");
                return false;
            }
            return true;
        }

        /** The options object in that slot; false, having thrown, when a field it gives is unusable. */
        bool ReadOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *verb, KCDC::Features::Navigation::QueryOptions &out) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() > index && !info[index]->IsNullOrUndefined() && !info[index]->IsObject()) {
                Throw(isolate, std::string(verb) + ": options must be an object");
                return false;
            }

            double radius = 2.0;
            double height = 4.0;
            if (!ReadDistance(info, index, "searchRadius", verb, kMaxSearchRadius, radius) || !ReadDistance(info, index, "searchHeight", verb, kMaxSearchHeight, height)) {
                return false;
            }

            KCDC::Features::Navigation::DoorPolicy policy = KCDC::Features::Navigation::DoorPolicy::Unlocked;
            const v8::Local<v8::Value> doors              = GivenField(info, index, "doors");
            if (!doors.IsEmpty()) {
                const std::string name = doors->IsString() ? v8pp::from_v8<std::string>(isolate, doors) : std::string();
                if (name == "open") {
                    policy = KCDC::Features::Navigation::DoorPolicy::Open;
                }
                else if (name == "any") {
                    policy = KCDC::Features::Navigation::DoorPolicy::Any;
                }
                else if (name == "none") {
                    policy = KCDC::Features::Navigation::DoorPolicy::None;
                }
                else if (name != "unlocked") {
                    Throw(isolate, std::string(verb) + ": doors must be \"unlocked\", \"open\", \"any\" or \"none\"");
                    return false;
                }
            }

            out = ResolveNavigation().Options(policy, static_cast<float>(radius), static_cast<float>(height));
            return true;
        }

        v8::Local<v8::Value> PointOrNull(v8::Isolate *isolate, const std::optional<glm::vec3> &point) {
            return point ? v8pp::convert<glm::vec3>::to_v8(isolate, *point) : v8::Local<v8::Value>(v8::Null(isolate));
        }

        v8::Local<v8::Value> PathToObject(v8::Isolate *isolate, const KCDC::Features::Navigation::Navmesh::Path &path) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            v8::Local<v8::Array> points    = v8::Array::New(isolate, static_cast<int>(path.points.size()));
            double length                  = 0.0;
            for (std::size_t i = 0; i < path.points.size(); ++i) {
                points->Set(context, static_cast<std::uint32_t>(i), v8pp::convert<glm::vec3>::to_v8(isolate, path.points[i])).Check();
                length += i > 0 ? glm::distance(path.points[i - 1], path.points[i]) : 0.0F;
            }
            result->Set(context, v8pp::to_v8(isolate, "points"), points).Check();
            result->Set(context, v8pp::to_v8(isolate, "complete"), v8pp::to_v8(isolate, path.complete)).Check();
            result->Set(context, v8pp::to_v8(isolate, "length"), v8pp::to_v8(isolate, length)).Check();
            return result;
        }

        v8::Local<v8::Value> RayToObject(v8::Isolate *isolate, const KCDC::Features::Navigation::Navmesh::RayHit &ray) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "hit"), v8pp::to_v8(isolate, ray.hit)).Check();
            result->Set(context, v8pp::to_v8(isolate, "point"), v8pp::convert<glm::vec3>::to_v8(isolate, ray.point)).Check();
            return result;
        }
    } // namespace

    void Navigation::JS_ClosestPoint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        glm::vec3 point {0.0F};
        KCDC::Features::Navigation::QueryOptions options;
        if (!ReadPoint(info, 0, point)) {
            Throw(isolate, "Navigation.closestPoint: expected (point, options?)");
            return;
        }
        if (!ReadOptions(info, 1, "Navigation.closestPoint", options)) {
            return;
        }
        info.GetReturnValue().Set(PointOrNull(isolate, ResolveNavigation().GetMesh().ClosestPoint(point, options)));
    }

    void Navigation::JS_FloorAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        glm::vec3 point {0.0F};
        KCDC::Features::Navigation::QueryOptions options;
        if (!ReadPoint(info, 0, point)) {
            Throw(isolate, "Navigation.floorAt: expected (point, options?)");
            return;
        }
        if (!ReadOptions(info, 1, "Navigation.floorAt", options)) {
            return;
        }
        const std::optional<float> floor = ResolveNavigation().GetMesh().FloorAt(point, options);
        info.GetReturnValue().Set(floor ? v8pp::to_v8(isolate, *floor) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    void Navigation::JS_FindPath(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        glm::vec3 from {0.0F};
        glm::vec3 to {0.0F};
        KCDC::Features::Navigation::QueryOptions options;
        if (!ReadPoint(info, 0, from) || !ReadPoint(info, 1, to)) {
            Throw(isolate, "Navigation.findPath: expected (from, to, options?)");
            return;
        }
        if (!ReadOptions(info, 2, "Navigation.findPath", options)) {
            return;
        }
        const std::optional<KCDC::Features::Navigation::Navmesh::Path> path = ResolveNavigation().GetMesh().FindPath(from, to, options);
        info.GetReturnValue().Set(path ? PathToObject(isolate, *path) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    void Navigation::JS_ValidatePatrol(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        Shared::Game::PatrolRouteDefinition route;
        std::string error;
        if (!info.Length() || !ReadPatrolRoute(isolate, info[0], route, error)) {
            Throw(isolate, error.empty() ? "Expected a PatrolRoute or its id" : error);
            return;
        }
        if (info.Length() > 1 && !info[1]->IsNullOrUndefined() && !info[1]->IsObject()) {
            Throw(isolate, "Navigation.validatePatrol: options must be an object");
            return;
        }
        auto policy      = Features::Navigation::DoorPolicy::Unlocked;
        auto world       = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        const auto actor = GivenField(info, 1, "actor");
        if (!actor.IsEmpty()) {
            const auto *npc   = actor->IsObject() ? v8pp::class_<Npc>::unwrap_object(isolate, actor) : nullptr;
            const auto *horse = actor->IsObject() ? v8pp::class_<Horse>::unwrap_object(isolate, actor) : nullptr;
            if (npc && npc->ResolveNpc()) {
                world = npc->ResolveNpc()->GetVirtualWorld();
                if (!npc->ResolveNpc()->CanOpenDoors()) {
                    policy = Features::Navigation::DoorPolicy::None;
                }
            }
            else if (horse && horse->ResolveHorse()) {
                policy = Features::Navigation::DoorPolicy::None;
            }
            else {
                Throw(isolate, "Navigation.validatePatrol: actor must be a live Npc or Horse");
                return;
            }
        }
        const fu2::function<bool(const Features::Navigation::EntityLink &) const> doors = [policy, world](const Features::Navigation::EntityLink &link) {
            return link.kind != Features::Navigation::TileCodec::kDoorLinkKind || (policy != Features::Navigation::DoorPolicy::None && Server::Get()->GetDoors().CanNavigate(link.entityGuid, world));
        };
        auto options          = ResolveNavigation().Options(policy, 2.0F, Features::Npc::NpcNavigation::kSearchHeight, Features::Npc::NpcOrders::kSearchNodes);
        options.linkGate      = &doors;
        const auto validation = Features::Patrol::ValidatePatrol(route, ResolveNavigation().GetMesh(), options);
        const auto context    = isolate->GetCurrentContext();
        auto result           = v8::Object::New(isolate);
        result->Set(context, v8pp::to_v8(isolate, "available"), v8pp::to_v8(isolate, validation.available)).Check();
        result->Set(context, v8pp::to_v8(isolate, "complete"), v8pp::to_v8(isolate, validation.complete)).Check();
        auto legs = v8::Array::New(isolate, static_cast<int>(validation.legs.size()));
        for (std::uint32_t i = 0; i < validation.legs.size(); ++i) {
            const auto &leg = validation.legs[i];
            auto item       = v8::Object::New(isolate);
            item->Set(context, v8pp::to_v8(isolate, "fromIndex"), v8pp::to_v8(isolate, static_cast<int>(leg.fromIndex))).Check();
            item->Set(context, v8pp::to_v8(isolate, "toIndex"), v8pp::to_v8(isolate, static_cast<int>(leg.toIndex))).Check();
            item->Set(context, v8pp::to_v8(isolate, "complete"), v8pp::to_v8(isolate, leg.path && leg.path->complete)).Check();
            item->Set(context, v8pp::to_v8(isolate, "path"), leg.path ? PathToObject(isolate, *leg.path) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            legs->Set(context, i, item).Check();
        }
        result->Set(context, v8pp::to_v8(isolate, "legs"), legs).Check();
        info.GetReturnValue().Set(result);
    }

    void Navigation::JS_CanReach(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        glm::vec3 from {0.0F};
        glm::vec3 to {0.0F};
        KCDC::Features::Navigation::QueryOptions options;
        if (!ReadPoint(info, 0, from) || !ReadPoint(info, 1, to)) {
            Throw(isolate, "Navigation.canReach: expected (from, to, options?)");
            return;
        }
        if (!ReadOptions(info, 2, "Navigation.canReach", options)) {
            return;
        }
        const std::optional<KCDC::Features::Navigation::Navmesh::Path> path = ResolveNavigation().GetMesh().FindPath(from, to, options);
        info.GetReturnValue().Set(path.has_value() && path->complete);
    }

    void Navigation::JS_Raycast(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        glm::vec3 from {0.0F};
        glm::vec3 to {0.0F};
        KCDC::Features::Navigation::QueryOptions options;
        if (!ReadPoint(info, 0, from) || !ReadPoint(info, 1, to)) {
            Throw(isolate, "Navigation.raycast: expected (from, to, options?)");
            return;
        }
        if (!ReadOptions(info, 2, "Navigation.raycast", options)) {
            return;
        }
        const std::optional<KCDC::Features::Navigation::Navmesh::RayHit> ray = ResolveNavigation().GetMesh().Raycast(from, to, options);
        info.GetReturnValue().Set(ray ? RayToObject(isolate, *ray) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    void Navigation::JS_RandomPoint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        // No centre at all is the whole mesh; the options still sit in the third slot.
        KCDC::Features::Navigation::QueryOptions options;
        if (info.Length() == 0 || info[0]->IsUndefined()) {
            if (ReadOptions(info, 2, "Navigation.randomPoint", options)) {
                info.GetReturnValue().Set(PointOrNull(isolate, ResolveNavigation().GetMesh().RandomPoint(options)));
            }
            return;
        }

        glm::vec3 center {0.0F};
        if (!ReadPoint(info, 0, center) || info.Length() < 2 || !info[1]->IsNumber()) {
            Throw(isolate, "Navigation.randomPoint: expected (), or (center, radius, options?)");
            return;
        }
        const double radius = v8pp::from_v8<double>(isolate, info[1]);
        if (!(radius > 0.0 && radius <= kMaxRandomRadius)) {
            Throw(isolate, "Navigation.randomPoint: radius must be in (0, 512]");
            return;
        }
        if (!ReadOptions(info, 2, "Navigation.randomPoint", options)) {
            return;
        }
        info.GetReturnValue().Set(PointOrNull(isolate, ResolveNavigation().GetMesh().RandomPoint(center, static_cast<float>(radius), options)));
    }

    void Navigation::JS_EnableOverlay(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            Throw(isolate, "Navigation.enableOverlay: expected (name)");
            return;
        }
        info.GetReturnValue().Set(ResolveNavigation().EnableOverlay(v8pp::from_v8<std::string>(isolate, info[0])));
    }

    void Navigation::JS_DisableOverlay(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            Throw(isolate, "Navigation.disableOverlay: expected (name)");
            return;
        }
        info.GetReturnValue().Set(ResolveNavigation().DisableOverlay(v8pp::from_v8<std::string>(isolate, info[0])));
    }

    void Navigation::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &path = ServerCatalog().data_type("NavigationPath", "A walk across the mesh.");
        path.add_property("points", "Vector3[]", "The corners to walk through, first the point on the mesh nearest `from`. Straight lines between them stay on the mesh.");
        path.add_property("complete", "boolean", "Whether it reaches `to`. False when `to` cannot be reached, and the path then ends at the nearest point the mesh allows.");
        path.add_property("length", "number", "Its length in metres.");

        auto &ray = ServerCatalog().data_type("NavigationRay", "How far a straight walk across the mesh gets.");
        ray.add_property("hit", "boolean", "Whether the edge of the mesh -- a wall, a drop, a locked door -- stops it before `to`.");
        ray.add_property("point", "Vector3", "Where it stops: the edge, or the point on the mesh under `to`.");

        v8pp::module navigation(isolate, ServerCatalog(), "Navigation",
            "Queries the level's navigation mesh on the server. The mesh includes walkable floors, bridges and stairs.\n\n"
            "Copy the game's `Data/Levels/<level>/recast.pak` to the server's `files/<level>/` directory. The server searches `files/` recursively at startup. "
            "An optional `mod.navmesh` in `server.json` takes priority and can name a game install, level folder or pak file. The game data must be supplied separately. "
            "Without a mesh, `ready` is false and geometry queries return null or false. Points are world-space metres, Z up. "
            "Every query takes an optional options object. `searchRadius` and `searchHeight` limit how far a point may be from the mesh, across the ground and up or down: 2 metres by default, up to 64, and 4 by default, up to 128. "
            "Keep `searchHeight` under a storey's height, or a point on one floor snaps to the floor above. `doors` picks which doors a query walks through: `unlocked`, the default, is how an NPC treats a door -- open or shut, it goes through unless the door is locked; "
            "`open` only through doors that stand open; `any` ignores locks; `none` treats every doorway as a wall. Door states are read in the global world, except validatePatrol with an actor, which uses that actor's world. A field given with the wrong type or out of range throws.");

        navigation.property("ready", []() { return ResolveNavigation().IsReady(); },
            v8pp::metadata::property_docs("boolean", "Whether a mesh loaded from `mod.navmesh` or the server's `files/` directory. False when no usable copy is found; the server log gives the reason."));
        navigation.property("overlays", []() { return ResolveNavigation().GetMesh().OverlayNames(); },
            v8pp::metadata::property_docs("string[]", "Available overlay names, sorted. Overlays contain replacement navigation data for quest states. Enabling one replaces the covered part of the mesh. Empty without a mesh."));
        navigation.property("activeOverlays", []() { return ResolveNavigation().GetMesh().ActiveOverlays(); },
            v8pp::metadata::property_docs("string[]", "Enabled overlays in activation order. Initially empty; the level starts with its base mesh."));

        navigation.function("validatePatrol", &Navigation::JS_ValidatePatrol,
            v8pp::metadata::docs("{ available: boolean; complete: boolean; legs: { fromIndex: number; toIndex: number; complete: boolean; path: NavigationPath | null }[] }",
                {v8pp::metadata::param("route", "PatrolRoute | string", false, "A registered route or its id."),
                    v8pp::metadata::param("options", "{ actor?: Npc | Horse }", true, "Optional actor chooses door policy in its virtual world: unlocked doors for humanoids, no doorways for horses and animals.")},
                "Checks all legs, including the closing leg of a loop and reverse legs of ping-pong. Uses the patrol runner's search budget. A single waypoint is checked against itself. Missing mesh reports available false; partial paths are incomplete. This does not start or change an actor's order."));
        navigation.function("closestPoint", &Navigation::JS_ClosestPoint,
            v8pp::metadata::docs("Vector3 | null",
                {
                    v8pp::metadata::param("point", "Vector3 | Partial<Vector3>", false, "The point to snap; omitted components default to zero."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look, and which doors count."),
                },
                "Snaps a point to the nearest walkable surface.", "The nearest point within the search limits, or null when none is found or no mesh is loaded."));
        navigation.function("floorAt", &Navigation::JS_FloorAt,
            v8pp::metadata::docs("number | null",
                {
                    v8pp::metadata::param("point", "Vector3 | Partial<Vector3>", false, "The point to measure; omitted components default to zero. Its z estimates the floor height and selects the storey."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look."),
                },
                "Returns the height of the nearest walkable surface, including upper floors, bridges and stairs.", "The height in metres, or null when none is found or no mesh is loaded."));
        navigation.function("findPath", &Navigation::JS_FindPath,
            v8pp::metadata::docs("NavigationPath | null",
                {
                    v8pp::metadata::param("from", "Vector3 | Partial<Vector3>", false, "Where the walk starts; omitted components default to zero."),
                    v8pp::metadata::param("to", "Vector3 | Partial<Vector3>", false, "Where it should end; omitted components default to zero."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look for each end, and which doors to walk through."),
                },
                "Plans a path around obstacles on the mesh. Plan when a destination changes; avoid recalculating every NPC's path each tick.", "The path, or null when an endpoint cannot be found, the query fails or no mesh is loaded. A partial path has `complete: false`."));
        navigation.function("canReach", &Navigation::JS_CanReach,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("from", "Vector3 | Partial<Vector3>", false, "Where the walk starts; omitted components default to zero."),
                    v8pp::metadata::param("to", "Vector3 | Partial<Vector3>", false, "Where it should end; omitted components default to zero."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look for each end, and which doors to walk through."),
                },
                "Checks reachability by calling `findPath`.", "True when a complete path exists; false otherwise, including when no mesh is loaded."));
        navigation.function("raycast", &Navigation::JS_Raycast,
            v8pp::metadata::docs("NavigationRay | null",
                {
                    v8pp::metadata::param("from", "Vector3 | Partial<Vector3>", false, "Where the walk starts; omitted components default to zero."),
                    v8pp::metadata::param("to", "Vector3 | Partial<Vector3>", false, "Where it heads; only its x and y are used, the walk follows the surface."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look for `from`, and which doors let the walk through."),
                },
                "Checks a straight line across the mesh and reports where it stops.", "The result, or null when `from` is off the mesh, the query fails or no mesh is loaded."));
        navigation.function("randomPoint", &Navigation::JS_RandomPoint,
            v8pp::metadata::docs("Vector3 | null",
                {
                    v8pp::metadata::param("center", "Vector3 | Partial<Vector3>", true,
                        "The point to scatter around; it has to be on the mesh, and omitted components default to zero. Omitted entirely, the point can be anywhere on the level's mesh; pass `undefined` for it and for `radius` to give options."),
                    v8pp::metadata::param("radius", "number", true, "How far from `center`, in metres, up to 512. Required with a centre."),
                    v8pp::metadata::param("options", kQueryOptionsType, true, "How far to look for `center`, and which doors the point may lie behind."),
                },
                "Returns a random walkable point reachable from `center` and no further from it across the ground than `radius`. A point drawn outside the circle is drawn again, a bounded number of times. Without a centre, samples the whole level, choosing each tile with equal probability.",
                "The point, or null when `center` is off the mesh, no draw lands within `radius`, the query fails or no mesh is loaded."));
        navigation.function("enableOverlay", &Navigation::JS_EnableOverlay,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "An entry of `overlays`.")}, "Enables an overlay. Where overlays overlap, the last enabled takes precedence. Subsequent queries use the updated mesh.",
                "False for a name the level does not ship, one already enabled, one whose tiles cannot be read or loaded (the server log says why), or without a mesh."));
        navigation.function("disableOverlay", &Navigation::JS_DisableOverlay,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "An entry of `activeOverlays`.")}, "Disables an overlay, restoring the previous enabled overlay or the base mesh in that area.",
                "False for a name that is not enabled, or when part of the area could not be restored (the server log says why); the overlay is disabled either way."));

        navigation.publish(global);
    }
} // namespace KCDC::Scripting
