#include "world_query_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"

#include "features/world/world_query_service.h"

#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/world/world_query_metadata.h"
#include "shared/features/world/world_query_rpc.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/option_field.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/builtins/entity.h>
#include <scripting/v8pp_types.h>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** The ground probe's reach when the caller does not say. */
        constexpr float kDefaultProbeUp   = 5.0F;
        constexpr float kDefaultProbeDown = 200.0F;

        // Unguarded on purpose: PostInit installs the service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::World::WorldQueryService &ResolveQueries() {
            return Server::Get()->GetWorldQueries();
        }

        /** The body the first argument names, or null. */
        KCDC::Shared::Entities::HumanEntity *CallerArg(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *handle      = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(info.GetIsolate(), info[0]) : nullptr;
            auto *replication = Framework::CoreModules::GetReplication();
            return handle != nullptr && replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(handle->GetId()) : nullptr;
        }

        /** A finite world-space point in that slot, or false. */
        bool ReadPoint(const v8::FunctionCallbackInfo<v8::Value> &info, int index, glm::vec3 &point) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return false;
            }
            point = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        v8::Local<v8::Value> HitToObject(v8::Isolate *isolate, const KCDC::Shared::RPC::WorldRayHit &hit) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3(hit.pointX, hit.pointY, hit.pointZ))).Check();
            result->Set(context, v8pp::to_v8(isolate, "normal"), v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3(hit.normalX, hit.normalY, hit.normalZ))).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, hit.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "surface"), v8pp::to_v8(isolate, hit.surface)).Check();
            result->Set(context, v8pp::to_v8(isolate, "terrain"), v8pp::to_v8(isolate, hit.terrain)).Check();
            result->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, WorldHitKindName(hit.kind))).Check();
            result->Set(context, v8pp::to_v8(isolate, "model"), !hit.model.empty() ? v8pp::to_v8(isolate, hit.model) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            const char *category = WorldHitCategory(hit.model);
            result->Set(context, v8pp::to_v8(isolate, "category"), category != nullptr ? v8pp::to_v8(isolate, category) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "material"), !hit.material.empty() ? v8pp::to_v8(isolate, hit.material) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            // Every entity has a class; only ones the level placed have a GUID.
            const bool struckEntity = !hit.entityClass.empty();
            result->Set(context, v8pp::to_v8(isolate, "entityGuid"), hit.entityGuid != 0 ? v8pp::to_v8(isolate, FormatLevelGuid(hit.entityGuid)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityName"), struckEntity ? v8pp::to_v8(isolate, hit.entityName) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "entityClass"), struckEntity ? v8pp::to_v8(isolate, hit.entityClass) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            const glm::quat rotation(hit.objectRotation[3], hit.objectRotation[0], hit.objectRotation[1], hit.objectRotation[2]);
            result
                ->Set(context, v8pp::to_v8(isolate, "levelObject"),
                    WorldLevelObjectToValue(isolate, hit.levelObject, hit.kind, hit.model, hit.entityName, hit.entityClass, hit.entityGuid, glm::vec3(hit.objectAt[0], hit.objectAt[1], hit.objectAt[2]), glm::normalize(rotation),
                        glm::vec3(hit.objectScale[0], hit.objectScale[1], hit.objectScale[2])))
                .Check();
            return result;
        }

        v8::Local<v8::Value> NearbyToObject(v8::Isolate *isolate, const KCDC::Shared::RPC::WorldNearbyEntity &entity) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3(entity.positionX, entity.positionY, entity.positionZ))).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, entity.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "guid"), entity.entityGuid != 0 ? v8pp::to_v8(isolate, FormatLevelGuid(entity.entityGuid)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
            result->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, entity.name)).Check();
            result->Set(context, v8pp::to_v8(isolate, "class"), v8pp::to_v8(isolate, entity.entityClass)).Check();
            result->Set(context, v8pp::to_v8(isolate, "physicalized"), v8pp::to_v8(isolate, entity.physicalized)).Check();
            return result;
        }

        /** What a trace verb resolves to. */
        v8::Local<v8::Value> TraceToObject(v8::Isolate *isolate, const KCDC::Features::World::WorldQueryService::Answer &answer) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "answered"), v8pp::to_v8(isolate, answer.answered)).Check();
            result->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, answer.reason)).Check();

            v8::Local<v8::Array> hits = v8::Array::New(isolate, static_cast<int>(answer.hits.size()));
            std::uint32_t index       = 0;
            for (const auto &hit : answer.hits) {
                hits->Set(context, index++, HitToObject(isolate, hit)).Check();
            }
            result->Set(context, v8pp::to_v8(isolate, "hits"), hits).Check();
            result->Set(context, v8pp::to_v8(isolate, "hit"), answer.hits.empty() ? v8::Local<v8::Value>(v8::Null(isolate)) : HitToObject(isolate, answer.hits.front())).Check();
            return result;
        }

        /** What the sphere verb resolves to. */
        v8::Local<v8::Value> RadiusToObject(v8::Isolate *isolate, const KCDC::Features::World::WorldQueryService::Answer &answer) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "answered"), v8pp::to_v8(isolate, answer.answered)).Check();
            result->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, answer.reason)).Check();

            v8::Local<v8::Array> entities = v8::Array::New(isolate, static_cast<int>(answer.entities.size()));
            std::uint32_t index           = 0;
            for (const auto &entity : answer.entities) {
                entities->Set(context, index++, NearbyToObject(isolate, entity)).Check();
            }
            result->Set(context, v8pp::to_v8(isolate, "entities"), entities).Check();
            return result;
        }

        /** Turns a settled answer into the object a verb promised. */
        using AnswerFormatter = v8::Local<v8::Value> (*)(v8::Isolate *, const KCDC::Features::World::WorldQueryService::Answer &);

        /** Resolve the caller, send the query, hand back a promise. A refusal here spends no request id. */
        void AskAndPromise(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage, KCDC::Shared::RPC::WorldQuery query, AnswerFormatter format) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            KCDC::Shared::Entities::HumanEntity *caller = CallerArg(info);
            if (caller == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": the first argument must be a player"));
                return;
            }

            // The wire's own bounds, so a resource is told what is wrong
            // rather than having the receiving peer drop it silently. The
            // request id is the one field still unset.
            query.requestId = 1;
            if (!query.IsSane()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": the query is out of range; see the argument docs for the limits"));
                return;
            }
            query.requestId = 0;

            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            info.GetReturnValue().Set(resolver->GetPromise());

            auto pending = std::make_shared<PendingPromise>(isolate, context, resolver);

            // The answer lands on the server tick, so the isolate is re-entered rather than held open.
            const bool asked = ResolveQueries().Ask(*caller, std::move(query), [pending, format](const KCDC::Features::World::WorldQueryService::Answer &answer) {
                pending->Settle([&](v8::Isolate *iso, v8::Local<v8::Context>) {
                    return format(iso, answer);
                });
            });

            if (!asked) {
                KCDC::Features::World::WorldQueryService::Answer refused;
                refused.reason = "The server has no peer to ask.";
                resolver->Resolve(context, format(isolate, refused)).Check();
                pending->Release();
            }
        }

        /** What a trace verb reads out of its arguments, up to how many hits it wants. */
        bool ReadTrace(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage, KCDC::Shared::RPC::WorldQuery &query) {
            v8::Isolate *isolate = info.GetIsolate();

            glm::vec3 from {};
            glm::vec3 to {};
            if (!ReadPoint(info, 1, from) || !ReadPoint(info, 2, to)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": expected (player, from, to, options?)"));
                return false;
            }

            const std::string mode = StringOption(isolate, info, 3, "mode");
            if (!mode.empty() && !ParseWorldRayMode(mode, query.rayMode)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": `" + mode + "` is not one of `cover`, `anything` or `ground`"));
                return false;
            }

            query.kind    = KCDC::Shared::RPC::WorldQueryKind::Raycast;
            query.originX = from.x;
            query.originY = from.y;
            query.originZ = from.z;
            query.targetX = to.x;
            query.targetY = to.y;
            query.targetZ = to.z;
            return true;
        }

        // JS: World.raycast(player, from, to, options?) -> Promise<WorldTraceResult>
        void JS_Raycast(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());

            KCDC::Shared::RPC::WorldQuery query;
            if (!ReadTrace(info, "World.raycast", query)) {
                return;
            }
            query.maxResults = 1;
            AskAndPromise(info, "World.raycast", std::move(query), &TraceToObject);
        }

        // JS: World.raycastAll(player, from, to, options?) -> Promise<WorldTraceResult>
        void JS_RaycastAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Shared::RPC::WorldQuery query;
            if (!ReadTrace(info, "World.raycastAll", query)) {
                return;
            }
            query.maxResults = static_cast<std::uint16_t>(std::clamp(NumberOption(isolate, info, 3, "maxHits", static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryHits)), 1.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryHits)));
            AskAndPromise(info, "World.raycastAll", std::move(query), &TraceToObject);
        }

        // JS: World.resolveGround(player, position, options?) -> Promise<WorldTraceResult>
        void JS_ResolveGround(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            glm::vec3 position {};
            if (!ReadPoint(info, 1, position)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "World.resolveGround: expected (player, position, options?)"));
                return;
            }

            KCDC::Shared::RPC::WorldQuery query;
            query.kind       = KCDC::Shared::RPC::WorldQueryKind::Ground;
            query.originX    = position.x;
            query.originY    = position.y;
            query.originZ    = position.z;
            query.probeUp    = static_cast<float>(std::clamp(NumberOption(isolate, info, 2, "up", kDefaultProbeUp), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));
            query.probeDown  = static_cast<float>(std::clamp(NumberOption(isolate, info, 2, "down", kDefaultProbeDown), 0.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryProbe)));
            query.maxResults = 1;
            AskAndPromise(info, "World.resolveGround", std::move(query), &TraceToObject);
        }

        // JS: World.entitiesInRadius(player, centre, radius, options?) -> Promise<WorldRadiusResult>
        void JS_EntitiesInRadius(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            glm::vec3 centre {};
            if (!ReadPoint(info, 1, centre) || info.Length() < 3 || !info[2]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "World.entitiesInRadius: expected (player, centre, radius, options?)"));
                return;
            }

            KCDC::Shared::RPC::WorldQuery query;
            query.kind         = KCDC::Shared::RPC::WorldQueryKind::Proximity;
            query.originX      = centre.x;
            query.originY      = centre.y;
            query.originZ      = centre.z;
            query.radius       = static_cast<float>(v8pp::from_v8<double>(isolate, info[2]));
            query.entityClass  = StringOption(isolate, info, 3, "class");
            query.physicalOnly = BoolOption(isolate, info, 3, "physicalOnly", false);
            query.maxResults   = static_cast<std::uint16_t>(std::clamp(NumberOption(isolate, info, 3, "max", static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryEntities)), 1.0, static_cast<double>(KCDC::Shared::RPC::kMaxWorldQueryEntities)));
            AskAndPromise(info, "World.entitiesInRadius", std::move(query), &RadiusToObject);
        }

        /** The script handle for whatever a replica turns out to be, or null for a type script has none for. */
        v8::Local<v8::Value> WrapReplica(v8::Isolate *isolate, Framework::Networking::Replication::NetworkEntity *entity) {
            const std::uint64_t networkId = entity->GetNetworkID();
            if (dynamic_cast<KCDC::Shared::Entities::HumanEntity *>(entity) != nullptr) {
                return WrapPlayer(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::HorseEntity *>(entity) != nullptr) {
                return WrapHorse(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::DogEntity *>(entity) != nullptr) {
                return WrapDog(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::PropEntity *>(entity) != nullptr) {
                return WrapProp(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::GroundItemEntity *>(entity) != nullptr) {
                return WrapGroundItem(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::DoorEntity *>(entity) != nullptr) {
                return WrapDoor(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::GateEntity *>(entity) != nullptr) {
                return WrapGate(isolate, networkId);
            }
            if (dynamic_cast<KCDC::Shared::Entities::StashEntity *>(entity) != nullptr) {
                return WrapStash(isolate, networkId);
            }
            return v8::Null(isolate);
        }

        // JS: World.replicasInRadius(position, radius, virtualWorld?) -> Entity[]
        void JS_ReplicasInRadius(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            if (info.Length() < 2 || !info[0]->IsObject() || !info[1]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "World.replicasInRadius: expected (position, radius, virtualWorld?)"));
                return;
            }

            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const double radius      = v8pp::from_v8<double>(isolate, info[1]);
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(radius) || radius <= 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "World.replicasInRadius: the position must be finite and the radius above 0"));
                return;
            }

            const MafiaNet::VirtualWorldId virtualWorld = info.Length() > 2 && info[2]->IsNumber() ? static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(isolate, info[2])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;
            const float radiusSquared                   = static_cast<float>(radius * radius);

            std::vector<std::pair<float, Framework::Networking::Replication::NetworkEntity *>> found;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEachEntity([&](Framework::Networking::Replication::NetworkEntity *entity) {
                    if (entity == nullptr || entity->GetVirtualWorld() != virtualWorld) {
                        return;
                    }
                    const glm::vec3 separation  = entity->position - position;
                    const float distanceSquared = glm::dot(separation, separation);
                    if (distanceSquared <= radiusSquared) {
                        found.emplace_back(distanceSquared, entity);
                    }
                });
            }

            std::sort(found.begin(), found.end(), [](const auto &left, const auto &right) {
                return left.first < right.first;
            });

            v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(found.size()));
            std::uint32_t index         = 0;
            for (const auto &entry : found) {
                result->Set(context, index++, WrapReplica(isolate, entry.second)).Check();
            }
            info.GetReturnValue().Set(result);
        }

        /** The cost every verb here carries, stated once. */
        const char *BorrowedWorldDocs() {
            return "\n\nThe server has no world of its own, so this asks that player's client and waits for it to answer. Three things follow: the answer is a round trip late; it covers only what that machine has streamed in, so a point far from the player reads as empty world; and it is a client's word, which is fine for a prompt and is not fine for anything a player gains by lying about. "
                   "The promise always settles -- on the answer, on a five-second timeout, or when the player leaves -- and `answered` says which.";
        }
    } // namespace

    namespace Events {
        void PlayerWeaponHit(std::uint64_t playerNetworkId, const KCDC::Shared::RPC::WorldRayHit &hit) {
            EmitReservedEvent("playerWeaponHit", [playerNetworkId, hit](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerNetworkId), HitToObject(isolate, hit)};
            });
        }
    } // namespace Events

    void RegisterWorldQueryBindings(v8::Isolate *isolate, v8pp::module &world) {
        RegisterWorldQueryMetadata(ServerCatalog(), /*includeEntityId=*/false);

        ServerCatalog()
            .data_type("EventMap")
            .add_property("playerWeaponHit", "[player: Player, hit: WorldRayHit]",
                "Dispatched when a player's swing meets something solid that is not a body: a tree, a wall, a rock, a prop, the ground. Once per thing per attack, at the first contact, as the game itself counts it. `hit` is named as a trace names it -- `category` tells a tree from a rock, and `levelObject` is what `LevelEdit.apply` takes to fell or break it -- with `distance` measured from the player's feet. "
                "It is that player's client's word, checked only to lie within 8 m of them and in the server's level, and at most 16 a second are taken from one player. Fists and kicks are not reported, and neither is a blow on a player, an NPC or an animal, which `playerHit` and the damage events cover.");

        auto &trace = ServerCatalog().data_type("WorldTraceResult", "What a trace came back with, and whether it came back at all.");
        trace.add_property("answered", "boolean", "Whether the client ran the trace. False when it has no world loaded, is in another level, did not answer in time, or left.");
        trace.add_property("reason", "string", "Why it did not answer. Empty when it did.");
        trace.add_property("hit", "WorldRayHit | null", "The nearest thing the ray met, or null when it met nothing and whenever `answered` is false.");
        trace.add_property("hits", "WorldRayHit[]", "Everything it met, nearest first. One entry at most unless `maxHits` asked for more; empty whenever `answered` is false.");

        auto &radius = ServerCatalog().data_type("WorldRadiusResult", "What a sphere came back with, and whether it came back at all.");
        radius.add_property("answered", "boolean", "Whether the client ran the query. False when it has no world loaded, is in another level, did not answer in time, or left.");
        radius.add_property("reason", "string", "Why it did not answer. Empty when it did.");
        radius.add_property("entities", "WorldNearbyEntity[]", "The entities inside the sphere, nearest first. Empty whenever `answered` is false.");

        const v8pp::metadata::parameter_options playerParam = v8pp::metadata::param("player", "Player", false, "Whose client is asked. Their machine is the one that answers, so pick a player near the point in question.");

        world.function("raycast", &JS_Raycast,
            v8pp::metadata::docs("Promise<WorldTraceResult>",
                {
                    playerParam,
                    v8pp::metadata::param("from", "Vector3", false, "Where the ray starts, in world-space metres."),
                    v8pp::metadata::param("to", "Vector3", false, "Where it ends. Up to 4096 metres away; a ray of no length is refused."),
                    v8pp::metadata::param("options", "{ mode?: \"cover\" | \"anything\" | \"ground\" }", true,
                        "`mode` picks which of the game's own three traces to run: `cover` asks whether the world is in the way -- static geometry, props and doors block, and bodies cannot, which is what a line of sight wants; `anything` asks what is under the ray, bodies included, which is what a pick wants; `ground` sees only surfaces a player could stand on. It defaults to `cover`, and a name that is none of the three is rejected rather than guessed at."),
                },
                std::string("Traces a segment through the world and reports the first thing it meets.").append(BorrowedWorldDocs()), "The trace, once that client has run it."));

        world.function("raycastAll", &JS_RaycastAll,
            v8pp::metadata::docs("Promise<WorldTraceResult>",
                {
                    playerParam,
                    v8pp::metadata::param("from", "Vector3", false, "Where the ray starts, in world-space metres."),
                    v8pp::metadata::param("to", "Vector3", false, "Where it ends. Up to 4096 metres away; a ray of no length is refused."),
                    v8pp::metadata::param("options", "{ mode?: \"cover\" | \"anything\" | \"ground\"; maxHits?: number }", true,
                        "`mode` is as `World.raycast` documents it. `maxHits` is how many things along the ray to report, up to 8 and 8 by default; each is a separate solid hit, found by re-tracing past the one before, so a window does not hide the wall it is set in."),
                },
                std::string("Traces a segment and reports everything solid along it, nearest first.").append(BorrowedWorldDocs()), "The trace, once that client has run it."));

        world.function("resolveGround", &JS_ResolveGround,
            v8pp::metadata::docs("Promise<WorldTraceResult>",
                {
                    playerParam,
                    v8pp::metadata::param("position", "Vector3", false, "The point to look under, in world-space metres. Its own z is where the probe is centred."),
                    v8pp::metadata::param("options", "{ up?: number; down?: number }", true,
                        "How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each."),
                },
                std::string(
                    "What is underfoot at a point: its height in `hit.position.z`, the slope it landed on, and what the surface is made of.\n\nA trace rather than a terrain height, so it stands on whatever is actually there -- a bridge, a floor, a castle roof -- and not on the heightmap underneath it. There is no bare-number form here, because a promise has to be able to say that nothing answered.")
                    .append(BorrowedWorldDocs()),
                "The probe, once that client has run it."));

        world.function("replicasInRadius", &JS_ReplicasInRadius,
            v8pp::metadata::docs("Entity[]",
                {
                    v8pp::metadata::param("position", "Vector3", false, "World-space point to measure from."),
                    v8pp::metadata::param("radius", "number", false, "How far to look, in metres."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one, where every body starts."),
                },
                "The server's own replicated entities near a point, nearest first -- players, horses, dogs, props, dropped items, doors, gates and stashes, each as its own handle.\n\nThis needs no client: the server already knows where its replicas are, so unlike `entitiesInRadius` it is immediate and authoritative. It sees only what the server "
                "replicates, which is the other half -- the level's own entities are what `entitiesInRadius` is for.",
                "The handles, nearest first."));

        world.function("entitiesInRadius", &JS_EntitiesInRadius,
            v8pp::metadata::docs("Promise<WorldRadiusResult>",
                {
                    playerParam,
                    v8pp::metadata::param("centre", "Vector3", false, "The centre of the sphere, in world-space metres."),
                    v8pp::metadata::param("radius", "number", false, "Its radius in metres, up to 256."),
                    v8pp::metadata::param("options", "{ class?: string; max?: number; physicalOnly?: boolean }", true,
                        "`class` reports only entities of that engine class -- `AnimDoor`, `NPC_NAI`, `GeomEntity` -- and a class the engine does not know matches nothing rather than everything; omitted, every class is reported. `max` is how many to report, nearest first, up to 64. `physicalOnly` skips entities the engine has built no physics for, which is the filter the game's own proximity query applies, and is off by default."),
                },
                std::string(
                    "Every entity that client's engine has inside a sphere, nearest first.\n\nIt reads the engine's own spatial grid, so it costs what the sphere covers rather than what the level holds. This reports the level's entities -- doors, props, bodies -- and not the server's replicas; correlate them by `guid`.")
                    .append(BorrowedWorldDocs()),
                "The entities, once that client has looked."));
    }
} // namespace KCDC::Scripting
