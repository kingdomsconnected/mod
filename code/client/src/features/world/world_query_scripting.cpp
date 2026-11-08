#include "world_query_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"

#include "features/player/player.h"
#include "features/player/player_scripting.h"
#include "features/world/world_query_service.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include "shared/features/world/world_query_metadata.h"
#include "shared/features/world/world_query_rpc.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** The ground probe's reach when the caller does not say. */
        constexpr float kDefaultProbeUp   = 5.0F;
        constexpr float kDefaultProbeDown = 200.0F;

        KCDC::Features::World::WorldQueryService *Queries() {
            return Core::gApplication ? &Core::gApplication->GetWorldQueryService() : nullptr;
        }

        /** A finite world-space point in that slot, or false. */
        bool ReadPoint(const v8::FunctionCallbackInfo<v8::Value> &info, int index, SDK::Vec3 &point) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return false;
            }
            const glm::vec3 value = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
            if (!KCDC::Common::IsFinite(value)) {
                return false;
            }
            point = KCDC::Common::ToNative(value);
            return true;
        }

        /** Throws on an unknown mode: a typo would otherwise become a line of sight and read as "nothing in the way". */
        bool ReadRayMode(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *usage, KCDC::Shared::RPC::WorldRayMode &mode) {
            const std::string name = StringOption(isolate, info, index, "mode");
            if (name.empty()) {
                mode = KCDC::Shared::RPC::WorldRayMode::Cover;
                return true;
            }
            if (!ParseWorldRayMode(name, mode)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": `" + name + "` is not one of `cover`, `anything` or `ground`"));
                return false;
            }
            return true;
        }

        /**
         * The player whose body a trace met, or null. The local player's body
         * is the local player entity, and everybody else's is their copy;
         * both colliders a body has name that same entity.
         */
        v8::Local<v8::Value> PlayerAt(v8::Isolate *isolate, SDK::EntityId entityId) {
            if (entityId == 0) {
                return v8::Null(isolate);
            }
            SDK::IEntity *local = KCDC::Common::LocalPlayer();
            auto *human         = KCDC::Common::FindReplica<KCDC::Features::Player::Human>([entityId, local](KCDC::Features::Player::Human *candidate) { return candidate->IsOwner() ? local != nullptr && local->GetId() == entityId : candidate->GetEntityId() == entityId; });
            if (human == nullptr) {
                return v8::Null(isolate);
            }
            Player::GetClass(isolate);
            return v8pp::class_<Player>::create_object(isolate, human->GetNetworkID());
        }

        v8::Local<v8::Value> HitToObject(v8::Isolate *isolate, const KCDC::Features::World::WorldQueryService::RayHit &hit) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(hit.point))).Check();
            result->Set(context, v8pp::to_v8(isolate, "normal"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(hit.normal))).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, hit.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "surface"), v8pp::to_v8(isolate, hit.surface)).Check();
            result->Set(context, v8pp::to_v8(isolate, "terrain"), v8pp::to_v8(isolate, hit.terrain)).Check();
            result->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, WorldHitKindName(hit.kind))).Check();
            result->Set(context, v8pp::to_v8(isolate, "model"), !hit.model.empty() ? v8pp::to_v8(isolate, hit.model) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            const char *category = WorldHitCategory(hit.model);
            result->Set(context, v8pp::to_v8(isolate, "category"), category != nullptr ? v8pp::to_v8(isolate, category) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "material"), !hit.material.empty() ? v8pp::to_v8(isolate, hit.material) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityGuid"), hit.entityGuid != 0 ? v8pp::to_v8(isolate, FormatLevelGuid(hit.entityGuid)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityName"), hit.entityId != 0 ? v8pp::to_v8(isolate, hit.entityName) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityClass"), hit.entityId != 0 ? v8pp::to_v8(isolate, hit.entityClass) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityId"), hit.entityId != 0 ? v8pp::to_v8(isolate, static_cast<double>(hit.entityId)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "player"), PlayerAt(isolate, hit.entityId)).Check();
            result
                ->Set(context, v8pp::to_v8(isolate, "levelObject"),
                    WorldLevelObjectToValue(isolate, hit.levelObject, hit.kind, hit.model, hit.entityName, hit.entityClass, hit.entityGuid, KCDC::Common::ToGlm(hit.levelObjectAt), KCDC::Common::ToGlm(hit.levelObjectRotation), KCDC::Common::ToGlm(hit.levelObjectScale)))
                .Check();
            return result;
        }

        v8::Local<v8::Value> NearbyToObject(v8::Isolate *isolate, const KCDC::Features::World::WorldQueryService::NearbyEntity &entity) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(entity.position))).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, entity.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "guid"), entity.entityGuid != 0 ? v8pp::to_v8(isolate, FormatLevelGuid(entity.entityGuid)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, entity.name)).Check();
            result->Set(context, v8pp::to_v8(isolate, "class"), v8pp::to_v8(isolate, entity.entityClass)).Check();
            result->Set(context, v8pp::to_v8(isolate, "physicalized"), v8pp::to_v8(isolate, entity.physicalized)).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityId"), v8pp::to_v8(isolate, static_cast<double>(entity.entityId))).Check();
            return result;
        }

        /** The half both trace verbs share, up to how many hits come back. */
        bool ReadRayRequest(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage, KCDC::Features::World::WorldQueryService::RayRequest &request) {
            v8::Isolate *isolate = info.GetIsolate();

            if (!ReadPoint(info, 0, request.origin) || !ReadPoint(info, 1, request.target)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": expected (from, to, options?)"));
                return false;
            }

            const SDK::Vec3 span {request.target.x - request.origin.x, request.target.y - request.origin.y, request.target.z - request.origin.z};
            const float lengthSquared = span.x * span.x + span.y * span.y + span.z * span.z;
            if (lengthSquared <= 0.0F || lengthSquared > KCDC::Shared::RPC::kMaxWorldQueryRayLength * KCDC::Shared::RPC::kMaxWorldQueryRayLength) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": the ray must be longer than nothing and no longer than 4096 metres"));
                return false;
            }

            request.ignoreLocalPlayer = BoolOption(isolate, info, 2, "ignoreSelf", true);
            return ReadRayMode(isolate, info, 2, usage, request.mode);
        }
    } // namespace

    void World::JS_Raycast(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::World::WorldQueryService::RayRequest request;
        if (!ReadRayRequest(info, "World.raycast", request)) {
            return;
        }
        request.maxHits = 1;

        std::vector<KCDC::Features::World::WorldQueryService::RayHit> hits;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries == nullptr || !queries->Raycast(request, hits) || hits.empty()) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(HitToObject(isolate, hits.front()));
    }

    void World::JS_RaycastAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        KCDC::Features::World::WorldQueryService::RayRequest request;
        if (!ReadRayRequest(info, "World.raycastAll", request)) {
            return;
        }
        request.maxHits = static_cast<int>(std::clamp(NumberOption(isolate, info, 2, "maxHits", static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryHits)), 1.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryHits)));

        std::vector<KCDC::Features::World::WorldQueryService::RayHit> hits;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries != nullptr) {
            queries->Raycast(request, hits);
        }

        v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(hits.size()));
        std::uint32_t index         = 0;
        for (const auto &hit : hits) {
            result->Set(context, index++, HitToObject(isolate, hit)).Check();
        }
        info.GetReturnValue().Set(result);
    }

    void World::JS_GetGroundZ(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 position {};
        if (!ReadPoint(info, 0, position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.getGroundZ: expected (position, options?)"));
            return;
        }

        const float probeUp   = static_cast<float>(std::clamp(NumberOption(isolate, info, 1, "up", kDefaultProbeUp), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));
        const float probeDown = static_cast<float>(std::clamp(NumberOption(isolate, info, 1, "down", kDefaultProbeDown), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));

        KCDC::Features::World::WorldQueryService::RayHit ground;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries == nullptr || !queries->ResolveGround(position, probeUp, probeDown, ground)) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(static_cast<double>(ground.point.z));
    }

    void World::JS_ResolveGround(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 position {};
        if (!ReadPoint(info, 0, position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.resolveGround: expected (position, options?)"));
            return;
        }

        const float probeUp   = static_cast<float>(std::clamp(NumberOption(isolate, info, 1, "up", kDefaultProbeUp), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));
        const float probeDown = static_cast<float>(std::clamp(NumberOption(isolate, info, 1, "down", kDefaultProbeDown), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));

        KCDC::Features::World::WorldQueryService::RayHit ground;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries == nullptr || !queries->ResolveGround(position, probeUp, probeDown, ground)) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(HitToObject(isolate, ground));
    }

    void World::JS_EntitiesInRadius(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        KCDC::Features::World::WorldQueryService::ProximityRequest request;
        if (!ReadPoint(info, 0, request.centre) || info.Length() < 2 || !info[1]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.entitiesInRadius: expected (centre, radius, options?)"));
            return;
        }

        const double radius = v8pp::from_v8<double>(isolate, info[1]);
        if (!std::isfinite(radius) || radius <= 0.0 || radius > static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryRadius)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "World.entitiesInRadius: the radius must be above 0 and no more than 256 metres"));
            return;
        }

        request.radius       = static_cast<float>(radius);
        request.entityClass  = StringOption(isolate, info, 2, "class");
        request.physicalOnly = BoolOption(isolate, info, 2, "physicalOnly", false);
        request.maxEntities  = static_cast<int>(std::clamp(NumberOption(isolate, info, 2, "max", static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryEntities)), 1.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryEntities)));

        std::vector<KCDC::Features::World::WorldQueryService::NearbyEntity> entities;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries != nullptr) {
            queries->EntitiesInRadius(request, entities);
        }

        v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(entities.size()));
        std::uint32_t index         = 0;
        for (const auto &entity : entities) {
            result->Set(context, index++, NearbyToObject(isolate, entity)).Check();
        }
        info.GetReturnValue().Set(result);
    }

    namespace Events {
        void WeaponHit(const KCDC::Features::World::WorldQueryService::RayHit &hit) {
            EmitReservedEvent("weaponHit", [hit](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {HitToObject(isolate, hit)};
            });
        }
    } // namespace Events

    void World::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        RegisterWorldQueryMetadata(ClientCatalog(), /*includeEntityId=*/true);

        ClientCatalog()
            .data_type("EventMap")
            .add_property("weaponHit", "[hit: WorldRayHit]",
                "Dispatched on this machine when the local player's swing meets something solid that is not a body: a tree, a wall, a rock, a prop, the ground. Once per thing per attack, at the first contact, as the game itself counts it; a blade that rests against a wall is not reported again. `hit` is named as a trace names it, with `distance` measured from the player's feet. Fists and kicks are not reported, and neither is a blow on a player, an NPC or an animal, which `playerHit` and the damage events cover. The server hears of it as `playerWeaponHit`.");

        auto &hit = ClientCatalog().data_type("WorldRayHit");
        hit.add_property("player", "Player | null",
            "The player whose body it was -- the local player's own included, when `ignoreSelf` is off -- or null for anything else. Found through the body's own entity, so it holds for a disguised player too: their collider is the mesh's size, and a trace that meets the mesh names them.");

        auto &nearby = ClientCatalog().data_type("WorldNearbyEntity");
        nearby.add_property("entityId", "number", "This machine's own handle for it. Local to this client and to this session -- it is not the network ID and it is not portable, so store `guid` instead.");

        v8pp::module world(isolate, ClientCatalog(), "World",
            "What this machine's engine can be asked about the level around it: what a trace meets, what the ground is under a point, and which entities are inside a sphere.\n\n"
            "Each is the engine's own query rather than a search over everything loaded, so the cost is what the question covers. They answer inside the call, because this is the machine that has the world -- the server's `World` asks the same three questions as promises, because it has to borrow a client's world to answer them.\n\n"
            "None of it is replicated and none of it is authority: an answer describes what one client has streamed in at one moment. A zone that matters is decided on the server, from an answer the server asked for itself.");

        world.function("raycast", &World::JS_Raycast,
            v8pp::metadata::docs("WorldRayHit | null",
                {
                    v8pp::metadata::param("from", "Vector3", false, "Where the ray starts, in world-space metres."),
                    v8pp::metadata::param("to", "Vector3", false, "Where it ends. Up to 4096 metres away; a ray of no length is refused."),
                    v8pp::metadata::param("options", "{ mode?: \"cover\" | \"anything\" | \"ground\"; ignoreSelf?: boolean }", true,
                        "`mode` picks which of the game's own three traces to run: `cover` asks whether the world is in the way -- static geometry, props and doors block, and bodies cannot, which is what a line of sight wants; `anything` asks what is under the ray, bodies included, which is what a pick wants; `ground` sees only surfaces a player could stand on. It defaults to `cover`, and a name that is none of the three is rejected rather than guessed at.\n\n`ignoreSelf` passes the local player's own body through, and is on by default: the player stands where the camera does, so a trace from the camera would otherwise mostly hit them. Both of a body's colliders are skipped, not just the entity's."),
                },
                "Traces a segment through the world and reports the first thing it meets.", "What it hit, or null when the ray met nothing and when this machine has no world loaded."));

        world.function("raycastAll", &World::JS_RaycastAll,
            v8pp::metadata::docs("WorldRayHit[]",
                {
                    v8pp::metadata::param("from", "Vector3", false, "Where the ray starts, in world-space metres."),
                    v8pp::metadata::param("to", "Vector3", false, "Where it ends. Up to 4096 metres away; a ray of no length is refused."),
                    v8pp::metadata::param("options", "{ mode?: \"cover\" | \"anything\" | \"ground\"; maxHits?: number; ignoreSelf?: boolean }", true,
                        "`mode` and `ignoreSelf` are as `World.raycast` documents them. `maxHits` is how many things along the ray to report, up to 8 and 8 by default."),
                },
                "Traces a segment and reports everything solid along it, nearest first. Each hit is found by re-tracing past the one before, so a window does not hide the wall it is set in.",
                "What the ray met, nearest first; empty when it met nothing and when this machine has no world loaded."));

        world.function("getGroundZ", &World::JS_GetGroundZ,
            v8pp::metadata::docs("number | null",
                {
                    v8pp::metadata::param("position", "Vector3", false, "The point to look under, in world-space metres. Its own z is where the probe is centred."),
                    v8pp::metadata::param("options", "{ up?: number; down?: number }", true,
                        "How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each."),
                },
                "The height of the ground under a point.\n\nA trace rather than a terrain height, so it stands on whatever is actually there -- a bridge, a floor, a castle roof -- and not on the heightmap underneath it.",
                "The world-space z of what is underfoot, or null when the probe reached nothing within its range."));

        world.function("resolveGround", &World::JS_ResolveGround,
            v8pp::metadata::docs("WorldRayHit | null",
                {
                    v8pp::metadata::param("position", "Vector3", false, "The point to look under, in world-space metres."),
                    v8pp::metadata::param("options", "{ up?: number; down?: number }", true,
                        "How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each."),
                },
                "The same probe as `getGroundZ`, with everything it found rather than only the height: the slope it landed on, and what the surface is made of.", "What is underfoot, or null when the probe reached nothing."));

        world.function("entitiesInRadius", &World::JS_EntitiesInRadius,
            v8pp::metadata::docs("WorldNearbyEntity[]",
                {
                    v8pp::metadata::param("centre", "Vector3", false, "The centre of the sphere, in world-space metres."),
                    v8pp::metadata::param("radius", "number", false, "Its radius in metres, up to 256."),
                    v8pp::metadata::param("options", "{ class?: string; max?: number; physicalOnly?: boolean }", true,
                        "`class` reports only entities of that engine class -- `AnimDoor`, `NPC_NAI`, `GeomEntity` -- and a class the engine does not know matches nothing rather than everything; omitted, every class is reported. `max` is how many to report, nearest first, up to 64. `physicalOnly` skips entities the engine has built no physics for, which is the filter the game's own proximity query applies, and is off by default."),
                },
                "Every entity the engine has inside a sphere, nearest first.\n\nThis reads the engine's own spatial grid, so it costs what the sphere covers rather than what the level holds -- but it only sees what this client has streamed in, which for a distant point is nothing.",
                "The entities inside it, nearest first."));

        world.publish(global);
    }
} // namespace KCDC::Scripting
