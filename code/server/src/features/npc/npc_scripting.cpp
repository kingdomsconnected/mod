#include "npc_scripting.h"
#include "core/server.h"
#include "features/patrol/patrol_scripting.h"
#include "shared/scripting/json_value.h"

#include "features/animation/animation_scripting.h"
#include "features/animation/animation_verbs.h"
#include "shared/scripting/animation_options.h"
#include "shared/scripting/option_field.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/inventory/inventory_scripting.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_verbs.h"

#include "shared/features/npc/npc_types.h"
#include "shared/features/player/player_appearance_bindings.h"
#include "shared/game/npc_roles.h"
#include "shared/game/npc_placement_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/npc_options.h"
#include "shared/scripting/rotation_arg.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        void Placements(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::string level = Server::Get()->GetModConfig().Get<std::string>("level");
            nlohmann::json rows = nlohmann::json::array();
            for (const auto &point : Shared::Game::g_NpcPlacements) {
                if (point.level != level) {
                    continue;
                }
                rows.push_back({{"name", point.name}, {"guid", point.guid}, {"level", point.level}, {"layer", point.layer}, {"soul", point.soul}, {"actorClass", point.actorClass}, {"position", {{"x", point.x}, {"y", point.y}, {"z", point.z}}}, {"rotation", {{"w", point.qw}, {"x", point.qx}, {"y", point.qy}, {"z", point.qz}}}});
            }
            info.GetReturnValue().Set(ParseJsonValue(info.GetIsolate(), rows.dump(), v8::Null(info.GetIsolate())));
        }

        Npc *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info.This());
        }

        Shared::Entities::NpcEntity *SelfNpc(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Npc *self = Self(info);
            return self != nullptr ? self->ResolveNpc() : nullptr;
        }

        void GetAnimalLoot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto *npc = Self(info);
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), npc != nullptr ? KCDC::Features::Npc::NpcInventoryModule::Get().AnimalLoot(npc->GetId()) : nlohmann::json(nullptr)));
        }

        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        std::string StringField(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, const std::string &fallback = {}) {
            const v8::Local<v8::Value> value = OptionField(isolate, options, key);
            return !value.IsEmpty() && value->IsString() ? v8pp::from_v8<std::string>(isolate, value) : fallback;
        }

        double NumberField(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, double fallback) {
            const v8::Local<v8::Value> value = OptionField(isolate, options, key);
            return !value.IsEmpty() && value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : fallback;
        }

        bool BoolField(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, bool fallback) {
            const v8::Local<v8::Value> value = OptionField(isolate, options, key);
            return !value.IsEmpty() ? value->BooleanValue(isolate) : fallback;
        }

        Shared::Game::NpcSpeed SpeedField(v8::Isolate *isolate, v8::Local<v8::Object> options, Shared::Game::NpcSpeed fallback) {
            return Shared::Game::NpcSpeedFromName(StringField(isolate, options, "speed"), fallback);
        }

        /** The options object an argument carries, or an empty handle. */
        v8::Local<v8::Object> OptionsArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return v8::Local<v8::Object>();
            }
            return info[index].As<v8::Object>();
        }

        glm::vec3 VectorArg(v8::Isolate *isolate, v8::Local<v8::Value> value, const glm::vec3 &fallback) {
            if (value.IsEmpty() || !value->IsObject()) {
                return fallback;
            }
            return v8pp::convert<glm::vec3>::from_v8(isolate, value);
        }

        /** The NetworkID an entity-or-id argument names, or 0. */
        std::uint64_t EntityIdArg(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            if (value.IsEmpty()) {
                return 0;
            }
            if (value->IsNumber()) {
                return static_cast<std::uint64_t>(v8pp::from_v8<double>(isolate, value));
            }
            if (auto *entity = v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, value)) {
                return entity->GetId();
            }
            return 0;
        }

        Shared::Game::NpcLocomotion ParseLocomotion(const std::string &mode) {
            if (mode == "native") {
                return Shared::Game::NpcLocomotion::Native;
            }
            if (mode == "kinematic") {
                return Shared::Game::NpcLocomotion::Kinematic;
            }
            throw std::runtime_error("Npc.locomotion: expected 'native' or 'kinematic' (case-sensitive)");
        }

        Shared::Game::NpcFlags FlagsFrom(v8::Isolate *isolate, v8::Local<v8::Object> options, Shared::Game::NpcFlags fallback, Shared::Game::NpcLocomotion locomotion) {
            const auto has = [&](Shared::Game::NpcFlags flag) {
                return Shared::Game::HasFlag(fallback, flag);
            };
            Shared::Game::NpcFlags flags = Shared::Game::NpcFlags::None;
            if (BoolField(isolate, options, "invulnerable", has(Shared::Game::NpcFlags::Invulnerable))) {
                flags |= Shared::Game::NpcFlags::Invulnerable;
            }
            if (BoolField(isolate, options, "frozen", has(Shared::Game::NpcFlags::Frozen))) {
                flags |= Shared::Game::NpcFlags::Frozen;
            }
            if (BoolField(isolate, options, "interactable", has(Shared::Game::NpcFlags::Interactable))) {
                flags |= Shared::Game::NpcFlags::Interactable;
            }
            if (BoolField(isolate, options, "nametag", has(Shared::Game::NpcFlags::ShowNametag))) {
                flags |= Shared::Game::NpcFlags::ShowNametag;
            }
            if (BoolField(isolate, options, "lootable", has(Shared::Game::NpcFlags::Lootable))) {
                flags |= Shared::Game::NpcFlags::Lootable;
            }
            if (locomotion == Shared::Game::NpcLocomotion::Native) {
                flags |= Shared::Game::NpcFlags::NativeSteering;
            }
            return flags;
        }

        /** Fills a spawn bag from an options object. */
        std::optional<KCDC::Features::Npc::SpawnParams> ParseSpawnOptions(v8::Isolate *isolate, v8::Local<v8::Object> options) {
            KCDC::Features::Npc::SpawnParams params;
            if (options.IsEmpty()) {
                return params;
            }

            // Read once: an accessor may change its result or throw. Only an
            // omitted/undefined option keeps the native default; null is invalid.
            v8::Local<v8::Value> locomotion;
            if (!options->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "locomotion")).ToLocal(&locomotion)) {
                return std::nullopt;
            }
            if (!locomotion->IsUndefined()) {
                if (!locomotion->IsString()) {
                    isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Npc.locomotion: expected 'native' or 'kinematic' (case-sensitive)")));
                    return std::nullopt;
                }
                try {
                    params.locomotion = ParseLocomotion(v8pp::from_v8<std::string>(isolate, locomotion));
                }
                catch (const std::runtime_error &error) {
                    isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, error.what())));
                    return std::nullopt;
                }
            }

            params.soul       = StringField(isolate, options, "soul");
            params.actorClass = StringField(isolate, options, "class");
            params.name       = StringField(isolate, options, "name");
            params.outfit     = StringField(isolate, options, "outfit");

            const v8::Local<v8::Value> wearing = OptionField(isolate, options, "wearing");
            if (!wearing.IsEmpty() && wearing->IsArray()) {
                v8::Local<v8::Array> list      = wearing.As<v8::Array>();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                for (std::uint32_t i = 0; i < list->Length(); ++i) {
                    v8::Local<v8::Value> item;
                    if (list->Get(context, i).ToLocal(&item) && item->IsString()) {
                        params.wearing.push_back(v8pp::from_v8<std::string>(isolate, item));
                    }
                }
            }

            const v8::Local<v8::Value> appearance = OptionField(isolate, options, "appearance");
            if (!appearance.IsEmpty()) {
                AppearanceFromV8(isolate, appearance, params.appearance);
            }

            params.position  = VectorArg(isolate, OptionField(isolate, options, "position"), params.position);
            params.rotation  = ParseRotationArg(isolate, OptionField(isolate, options, "rotation"), params.rotation);
            params.factionId = static_cast<std::uint8_t>(std::clamp(NumberField(isolate, options, "faction", 0.0), 0.0, 255.0));
            params.maxHealth = static_cast<float>(std::clamp(NumberField(isolate, options, "maxHealth", 100.0), 1.0, 100000.0));
            params.health    = static_cast<float>(std::clamp(NumberField(isolate, options, "health", params.maxHealth), 0.0, static_cast<double>(params.maxHealth)));
            params.flags     = FlagsFrom(isolate, options, params.flags, params.locomotion);

            const v8::Local<v8::Value> world = OptionField(isolate, options, "virtualWorld");
            if (!world.IsEmpty() && world->IsNumber()) {
                params.virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, world));
            }
            return params;
        }

        void EmitNpcEvent(const char *eventName, std::uint64_t npcNetworkId) {
            EmitReservedEvent(eventName, [npcNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapNpc(isolate, npcNetworkId));
                return args;
            });
        }

        // JS: Npc.create(options) -> Npc
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            const auto params = ParseSpawnOptions(isolate, OptionsArg(info, 0));
            if (!params) {
                return;
            }
            auto *npc = KCDC::Features::Npc::Npcs::Spawn(*params);
            if (npc == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Npc.create: invalid transform, mismatched animal class, unsupported animal appearance, or entity creation failed"));
                return;
            }
            info.GetReturnValue().Set(WrapNpc(isolate, npc->GetNetworkID()));
        }

        // JS: Npc.adopt(levelGuid, options?) -> Npc
        void JS_Adopt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Npc.adopt: expected (levelGuid, options?)"));
                return;
            }
            const auto levelGuid = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            const auto params    = ParseSpawnOptions(isolate, OptionsArg(info, 1));
            if (!params) {
                return;
            }
            auto *npc = KCDC::Features::Npc::Npcs::Adopt(levelGuid, *params);
            if (npc == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Npc.adopt: failed to create entity"));
                return;
            }
            info.GetReturnValue().Set(WrapNpc(isolate, npc->GetNetworkID()));
        }

        // JS: Npc.all(virtualWorld?) -> Npc[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> npcs      = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *npc : KCDC::Features::Npc::Npcs::Collect(WorldArg(info, 0))) {
                npcs->Set(context, index++, WrapNpc(isolate, npc->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(npcs);
        }

        // JS: Npc.getById(id) -> Npc | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Npc.getById: expected (id)"));
                return;
            }
            const auto networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapNpc(isolate, networkId));
        }

        // JS: Npc.removeAll(virtualWorld?) -> number
        void JS_RemoveAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Npc::Npcs::DestroyAll(WorldArg(info, 0))));
        }

        // JS: Npc.roles() -> string[]
        void JS_Roles(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::NpcRoleEntry &role : Shared::Game::g_NpcRoles) {
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(role.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }

        void JS_Animals(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate       = info.GetIsolate();
            auto context        = isolate->GetCurrentContext();
            auto rows           = v8::Array::New(isolate);
            std::uint32_t index = 0;
            for (const auto &animal : Shared::Game::g_Animals) {
                auto row = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(animal.name))).Check();
                row->Set(context, v8pp::to_v8(isolate, "soul"), v8pp::to_v8(isolate, std::string(animal.soulGuid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "actorClass"), v8pp::to_v8(isolate, std::string(animal.actorClass))).Check();
                rows->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(rows);
        }

        void JS_AnimalSpawnpoints(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate           = info.GetIsolate();
            auto context            = isolate->GetCurrentContext();
            const std::string level = info.Length() > 0 && info[0]->IsString() ? std::string(v8pp::from_v8<std::string>(isolate, info[0])) : KCDC::Server::Get()->GetModConfig().Get<std::string>("level");
            auto rows               = v8::Array::New(isolate);
            std::uint32_t index     = 0;
            for (const auto &point : Shared::Game::g_AnimalSpawnpoints) {
                if (level != "*" && point.level != level) {
                    continue;
                }
                auto row               = v8::Object::New(isolate);
                const auto stringField = [&](const char *key, std::string_view value) {
                    row->Set(context, v8pp::to_v8(isolate, key), v8pp::to_v8(isolate, std::string(value))).Check();
                };
                stringField("level", point.level);
                stringField("guid", point.guid);
                stringField("name", point.name);
                stringField("layer", point.layer);
                stringField("soul", point.soulGuid);
                stringField("actorClass", point.actorClass);
                stringField("spawnAreaGuid", point.spawnAreaGuid);
                row->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, point.position)).Check();
                row->Set(context, v8pp::to_v8(isolate, "count"), v8pp::to_v8(isolate, point.count)).Check();
                row->Set(context, v8pp::to_v8(isolate, "respawnDays"), v8pp::to_v8(isolate, point.respawnDays)).Check();
                row->Set(context, v8pp::to_v8(isolate, "spawnInFlock"), v8pp::to_v8(isolate, point.spawnInFlock)).Check();
                rows->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(rows);
        }

        // JS: npc.remove()
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Npc::Npcs::Destroy(SelfNpc(info));
        }

        // JS: npc.hold()
        void JS_Hold(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Npc::Npcs::Hold(SelfNpc(info));
        }

        bool ReadPathfinding(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb, KCDC::Features::Npc::Pathfinding &out) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() > 1 && !info[1]->IsNullOrUndefined() && !info[1]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": options must be an object"));
                return false;
            }
            v8::Local<v8::Value> value = v8::Undefined(isolate);
            if (info.Length() > 1 && info[1]->IsObject() && !info[1].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "pathfinding")).ToLocal(&value)) {
                return false;
            }
            out = KCDC::Features::Npc::Pathfinding::Auto;
            if (value->IsUndefined()) {
                return true;
            }
            if (value->IsString()) {
                const std::string mode = v8pp::from_v8<std::string>(isolate, value);
                if (mode == "auto") {
                    return true;
                }
                if (mode == "game") {
                    out = KCDC::Features::Npc::Pathfinding::Game;
                    return true;
                }
                if (mode == "server") {
                    out = KCDC::Features::Npc::Pathfinding::Server;
                    return true;
                }
            }
            isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": pathfinding must be \"auto\", \"game\" or \"server\""));
            return false;
        }

        // JS: npc.moveTo(position, options?) -> boolean
        void JS_MoveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                info.GetReturnValue().Set(false);
                return;
            }
            const glm::vec3 point              = VectorArg(isolate, info[0], glm::vec3(0.0F));
            v8::Local<v8::Object> options      = OptionsArg(info, 1);
            const Shared::Game::NpcSpeed speed = SpeedField(isolate, options, Shared::Game::NpcSpeed::Walk);
            const auto radius                  = static_cast<float>(NumberField(isolate, options, "radius", 1.5));
            KCDC::Features::Npc::Pathfinding pathfinding;
            if (!ReadPathfinding(info, "npc.moveTo", pathfinding)) {
                return;
            }
            try {
                info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::MoveTo(npc, point, speed, radius, pathfinding));
            }
            catch (const std::runtime_error &error) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("npc.moveTo: ") + error.what()));
            }
        }

        // JS: npc.patrol(points, options?) -> boolean
        void JS_Patrol(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1) {
                isolate->ThrowError(v8pp::to_v8(isolate, "npc.patrol: expected (points, options?)"));
                return;
            }

            if (!info[0]->IsArray()) {
                Shared::Game::PatrolRouteDefinition route;
                std::string error;
                if (!ReadPatrolRoute(isolate, info[0], route, error) || (info.Length() > 1 && !ApplyPatrolOptions(isolate, info[1], route, error))) {
                    isolate->ThrowError(v8pp::to_v8(isolate, error));
                    return;
                }
                KCDC::Features::Npc::Pathfinding pathfinding;
                if (!ReadPathfinding(info, "npc.patrol", pathfinding)) {
                    return;
                }
                npc = SelfNpc(info); // Options may invoke user getters that remove the actor.
                try {
                    info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::Patrol(npc, route, pathfinding));
                }
                catch (const std::runtime_error &exception) {
                    isolate->ThrowError(v8pp::to_v8(isolate, exception.what()));
                }
                return;
            }

            v8::Local<v8::Array> list      = info[0].As<v8::Array>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            std::vector<glm::vec3> route;
            route.reserve(list->Length());
            for (std::uint32_t i = 0; i < list->Length(); ++i) {
                v8::Local<v8::Value> item;
                if (list->Get(context, i).ToLocal(&item) && item->IsObject()) {
                    route.push_back(VectorArg(isolate, item, glm::vec3(0.0F)));
                }
            }

            v8::Local<v8::Object> options = OptionsArg(info, 1);
            const bool loop               = BoolField(isolate, options, "loop", true);
            const auto waitSeconds        = static_cast<float>(NumberField(isolate, options, "waitSeconds", 0.0));
            KCDC::Features::Npc::Pathfinding pathfinding;
            if (!ReadPathfinding(info, "npc.patrol", pathfinding)) {
                return;
            }
            try {
                if (!OptionField(isolate, options, "mode").IsEmpty() || !OptionField(isolate, options, "radius").IsEmpty()) {
                    Shared::Game::PatrolRouteDefinition definition;
                    definition.mode        = loop ? Shared::Game::PatrolMode::Loop : Shared::Game::PatrolMode::Once;
                    definition.speed       = SpeedField(isolate, options, Shared::Game::NpcSpeed::Walk);
                    definition.waitSeconds = std::clamp(waitSeconds, 0.0F, Shared::Game::PatrolRouteDefinition::kMaxWaitSeconds);
                    for (const auto &point : route) {
                        definition.points.push_back({point});
                    }
                    std::string error;
                    if (!ApplyPatrolOptions(isolate, options, definition, error)) {
                        isolate->ThrowError(v8pp::to_v8(isolate, error));
                        return;
                    }
                    info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::Patrol(SelfNpc(info), definition, pathfinding));
                    return;
                }
                const auto speed = SpeedField(isolate, options, Shared::Game::NpcSpeed::Walk);
                info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::Patrol(SelfNpc(info), route, loop, speed, waitSeconds, pathfinding));
            }
            catch (const std::runtime_error &error) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("npc.patrol: ") + error.what()));
            }
        }

        void JS_Attack(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::uint64_t targetId = info.Length() >= 1 ? EntityIdArg(info.GetIsolate(), info[0]) : 0;
            const auto weapon            = StringField(info.GetIsolate(), OptionsArg(info, 1), "weapon", "melee");
            info.GetReturnValue().Set((weapon == "melee" || weapon == "bow") && KCDC::Features::Npc::Npcs::Attack(SelfNpc(info), targetId, weapon == "bow"));
        }

        // JS: npc.follow(target, options?) -> boolean
        void JS_Follow(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc                    = SelfNpc(info);
            const std::uint64_t targetId = info.Length() >= 1 ? EntityIdArg(isolate, info[0]) : 0;
            if (npc == nullptr || targetId == 0) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Object> options = OptionsArg(info, 1);
            KCDC::Features::Npc::Npcs::Follow(npc, targetId, SpeedField(isolate, options, Shared::Game::NpcSpeed::Jog), static_cast<float>(NumberField(isolate, options, "radius", 3.0)));
            info.GetReturnValue().Set(true);
        }

        // JS: npc.flee(from, options?) -> boolean
        void JS_Flee(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc                    = SelfNpc(info);
            const std::uint64_t threatId = info.Length() >= 1 ? EntityIdArg(isolate, info[0]) : 0;
            if (npc == nullptr || (threatId == 0 && (info.Length() < 1 || !info[0]->IsObject()))) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Object> options = OptionsArg(info, 1);
            const glm::vec3 from          = threatId == 0 ? VectorArg(isolate, info[0], glm::vec3(0.0F)) : glm::vec3(0.0F);
            info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::Flee(npc, from, threatId, SpeedField(isolate, options, Shared::Game::NpcSpeed::Run), static_cast<float>(NumberField(isolate, options, "radius", 25.0))));
        }

        // JS: npc.lookAt(target) -> boolean
        void JS_LookAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1) {
                info.GetReturnValue().Set(false);
                return;
            }
            const std::uint64_t targetId = EntityIdArg(isolate, info[0]);
            const glm::vec3 point        = targetId == 0 ? VectorArg(isolate, info[0], glm::vec3(0.0F)) : glm::vec3(0.0F);
            KCDC::Features::Npc::Npcs::LookAt(npc, targetId, point);
            info.GetReturnValue().Set(true);
        }

        // JS: npc.teleport(position, rotation?) -> boolean
        void JS_Teleport(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                info.GetReturnValue().Set(false);
                return;
            }
            const glm::vec3 position = VectorArg(isolate, info[0], npc->position);
            const glm::quat rotation = ParseRotationArg(isolate, info.Length() >= 2 ? info[1] : v8::Local<v8::Value>(), npc->rotation);
            KCDC::Features::Npc::Npcs::Teleport(npc, position, rotation);
            info.GetReturnValue().Set(true);
        }

        // JS: npc.playAnimation(id) -> boolean
        //     npc.playAnimation(fragment, options?) -> boolean
        void JS_PlayAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            // A number is the emote catalog's row, the one-shot gesture this
            // verb played before it took fragments; kept so a resource
            // written against that still works.
            if (info.Length() >= 1 && info[0]->IsNumber()) {
                const auto id = static_cast<std::uint16_t>(v8pp::from_v8<double>(isolate, info[0]));
                info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::PlayAnimation(SelfNpc(info), id));
                return;
            }
            Shared::Game::AnimationPlayback request;
            if (!Animations::ParseRequest(info, 0, "Npc.playAnimation", request)) {
                return;
            }
            auto *npc = SelfNpc(info);
            info.GetReturnValue().Set(npc != nullptr && npc->alive && KCDC::Features::Animation::Animations::Play(npc, std::move(request)));
        }

        // JS: npc.stopAnimation(options?) -> boolean
        void JS_StopAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(KCDC::Features::Animation::Animations::Stop(SelfNpc(info), Animations::ParseStandUp(info, 0)));
        }

        // JS: npc.setFacialExpression(fragment, options?) -> boolean
        void JS_SetFacialExpression(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            std::string fragment;
            std::string tags;
            if (!Animations::ParseExpression(info, 0, "Npc.setFacialExpression", fragment, tags)) {
                return;
            }
            auto *npc = SelfNpc(info);
            info.GetReturnValue().Set(npc != nullptr && npc->alive && KCDC::Features::Animation::Animations::SetExpression(npc, std::move(fragment), std::move(tags)));
        }

        // JS: npc.setTalking(talking, style?) -> boolean
        void JS_SetTalking(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Game::TalkStyle style = Shared::Game::TalkStyle::None;
            if (!Animations::ParseTalking(info, 0, "Npc.setTalking", style)) {
                return;
            }
            auto *npc = SelfNpc(info);
            info.GetReturnValue().Set(npc != nullptr && npc->alive && KCDC::Features::Animation::Animations::SetTalking(npc, style));
        }

        // JS: npc.say(text) -> boolean
        void JS_Say(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate   = info.GetIsolate();
            const std::string text = info.Length() >= 1 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::Speak(SelfNpc(info), text));
        }

        // JS: npc.wear(itemClassGuids) -> boolean
        void JS_Wear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1 || !info[0]->IsArray()) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Array> list      = info[0].As<v8::Array>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            std::vector<std::string> guids;
            guids.reserve(list->Length());
            for (std::uint32_t i = 0; i < list->Length(); ++i) {
                v8::Local<v8::Value> item;
                if (list->Get(context, i).ToLocal(&item) && item->IsString()) {
                    guids.push_back(v8pp::from_v8<std::string>(isolate, item));
                }
            }
            info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::SetWearing(npc, guids));
        }

        // JS: npc.setOutfit(clothingPresetGuid) -> boolean
        void JS_SetOutfit(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate     = info.GetIsolate();
            const std::string preset = info.Length() >= 1 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            info.GetReturnValue().Set(KCDC::Features::Npc::Npcs::SetOutfit(SelfNpc(info), preset));
        }

        // JS: npc.setAppearance(appearance) -> boolean
        void JS_SetAppearance(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *npc = SelfNpc(info);
            if (npc == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "npc.setAppearance: expected (appearance)"));
                return;
            }
            // Absent keys keep what it is already wearing, exactly as a
            // player's does.
            Shared::Game::CharacterAppearance appearance = npc->appearance;
            if (!AppearanceFromV8(isolate, info[0], appearance)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "npc.setAppearance: every part must be a string, and gender must be \"male\" or \"female\""));
                return;
            }
            KCDC::Features::Npc::Npcs::SetAppearance(npc, appearance);
            info.GetReturnValue().Set(true);
        }

        // JS: npc.damage(amount, attacker?)
        void JS_Damage(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const auto amount    = info.Length() >= 1 && info[0]->IsNumber() ? static_cast<float>(v8pp::from_v8<double>(isolate, info[0])) : 0.0F;
            KCDC::Features::Npc::Npcs::Damage(SelfNpc(info), info.Length() >= 2 ? EntityIdArg(isolate, info[1]) : 0, amount);
        }

        // JS: npc.kill(attacker?)
        void JS_Kill(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Npc::Npcs::Kill(SelfNpc(info), info.Length() >= 1 ? EntityIdArg(info.GetIsolate(), info[0]) : 0);
        }

        // JS: npc.revive()
        void JS_Revive(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Npc::Npcs::Revive(SelfNpc(info));
        }

        // JS: npc.pin(player | null)
        void JS_Pin(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::uint64_t playerId = info.Length() >= 1 ? EntityIdArg(info.GetIsolate(), info[0]) : 0;
            KCDC::Features::Npc::Npcs::PinSimulator(SelfNpc(info), playerId);
        }

    } // namespace

    Shared::Entities::NpcEntity *Npc::ResolveNpc() const {
        return dynamic_cast<Shared::Entities::NpcEntity *>(Resolve());
    }

    std::string Npc::GetSoul() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->soulGuid : std::string();
    }

    std::string Npc::GetActorClass() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? std::string(npc->SpawnClass()) : std::string();
    }

    double Npc::GetLevelGuid() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? static_cast<double>(npc->levelGuid) : 0.0;
    }

    std::string Npc::GetName() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->name : std::string();
    }

    void Npc::SetName(const std::string &name) {
        KCDC::Features::Npc::Npcs::SetName(ResolveNpc(), name);
    }

    double Npc::GetFaction() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? static_cast<double>(npc->factionId) : 0.0;
    }

    void Npc::SetFaction(double faction) {
        KCDC::Features::Npc::Npcs::SetFaction(ResolveNpc(), static_cast<std::uint8_t>(std::clamp(faction, 0.0, 255.0)));
    }

    float Npc::GetHealth() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->health : 0.0F;
    }

    void Npc::SetHealth(float health) {
        KCDC::Features::Npc::Npcs::SetHealth(ResolveNpc(), health);
    }

    float Npc::GetMaxHealth() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->maxHealth : 0.0F;
    }

    bool Npc::GetAlive() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && npc->alive;
    }

    namespace {
        /** Turns one flag on or off through the verb, leaving the rest alone. */
        void ToggleFlag(Shared::Entities::NpcEntity *npc, Shared::Game::NpcFlags flag, bool value) {
            if (npc == nullptr) {
                return;
            }
            const auto current = static_cast<std::uint16_t>(npc->flags);
            const auto bit     = static_cast<std::uint16_t>(flag);
            KCDC::Features::Npc::Npcs::SetFlags(npc, static_cast<Shared::Game::NpcFlags>(value ? current | bit : current & ~bit));
        }

        /** Changes one part of the tag through the verb, leaving the rest alone. */
        template <typename Edit>
        void EditNametag(Shared::Entities::NpcEntity *npc, Edit edit) {
            if (npc == nullptr) {
                return;
            }
            Framework::Networking::Replication::NametagState next = npc->nametag;
            edit(next);
            KCDC::Features::Npc::Npcs::SetNametag(npc, next);
        }
    } // namespace

    bool Npc::GetInvulnerable() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Invulnerable);
    }

    void Npc::SetInvulnerable(bool value) {
        ToggleFlag(ResolveNpc(), Shared::Game::NpcFlags::Invulnerable, value);
    }

    bool Npc::GetFrozen() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Frozen);
    }

    void Npc::SetFrozen(bool value) {
        ToggleFlag(ResolveNpc(), Shared::Game::NpcFlags::Frozen, value);
    }

    bool Npc::GetInteractable() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Interactable);
    }

    void Npc::SetInteractable(bool value) {
        ToggleFlag(ResolveNpc(), Shared::Game::NpcFlags::Interactable, value);
    }

    bool Npc::GetShowNametag() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::ShowNametag);
    }

    void Npc::SetShowNametag(bool value) {
        ToggleFlag(ResolveNpc(), Shared::Game::NpcFlags::ShowNametag, value);
    }

    bool Npc::IsNametagHealthVisible() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && npc->nametag.Has(Framework::Networking::Replication::NametagComponent::Health);
    }

    void Npc::SetNametagHealthVisible(bool visible) {
        EditNametag(ResolveNpc(), [visible](Framework::Networking::Replication::NametagState &next) {
            next.Set(Framework::Networking::Replication::NametagComponent::Health, visible);
        });
    }

    std::string Npc::GetNametagText() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->nametag.text : std::string();
    }

    void Npc::SetNametagText(const std::string &text) {
        EditNametag(ResolveNpc(), [&text](Framework::Networking::Replication::NametagState &next) {
            next.text = text;
        });
    }

    std::uint32_t Npc::GetNametagColor() const {
        auto *npc = ResolveNpc();
        return npc != nullptr ? npc->nametag.color : 0xFFFFFFFF;
    }

    void Npc::SetNametagColor(std::uint32_t color) {
        EditNametag(ResolveNpc(), [color](Framework::Networking::Replication::NametagState &next) {
            next.color = color;
        });
    }

    bool Npc::GetLootable() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && Shared::Game::HasFlag(npc->flags, Shared::Game::NpcFlags::Lootable);
    }

    void Npc::SetLootable(bool value) {
        ToggleFlag(ResolveNpc(), Shared::Game::NpcFlags::Lootable, value);
    }

    std::string Npc::GetLocomotion() const {
        auto *npc = ResolveNpc();
        return npc != nullptr && npc->locomotion == Shared::Game::NpcLocomotion::Native ? "native" : "kinematic";
    }

    void Npc::SetLocomotion(const std::string &mode) {
        const auto locomotion = ParseLocomotion(mode);
        KCDC::Features::Npc::Npcs::SetLocomotion(ResolveNpc(), locomotion);
    }

    std::string Npc::GetIntent() const {
        auto *npc = ResolveNpc();
        if (npc == nullptr) {
            return "hold";
        }
        switch (KCDC::Features::Npc::Npcs::GetIntentKind(npc)) {
        case Shared::Game::NpcIntentKind::MoveTo: return "moveTo";
        case Shared::Game::NpcIntentKind::Follow: return "follow";
        case Shared::Game::NpcIntentKind::Attack: return "attack";
        case Shared::Game::NpcIntentKind::Flee: return "flee";
        case Shared::Game::NpcIntentKind::LookAt: return "lookAt";
        case Shared::Game::NpcIntentKind::PlayAnim: return "playAnim";
        case Shared::Game::NpcIntentKind::Talk: return "talk";
        case Shared::Game::NpcIntentKind::Hold:
        default: return "hold";
        }
    }

    std::string Npc::GetStatus() const {
        auto *npc = ResolveNpc();
        if (npc == nullptr) {
            return "idle";
        }
        return Shared::Game::NpcIntentStatusName(KCDC::Features::Npc::Npcs::GetStatus(npc));
    }

    v8::Local<v8::Value> Npc::GetPatrolState(v8::Isolate *isolate) const {
        auto *npc         = ResolveNpc();
        const auto *order = npc ? Features::Npc::NpcOrders::Get().Find(npc->GetNetworkID()) : nullptr;
        return ParseJsonValue(isolate, (order ? order->PatrolState() : nlohmann::json(nullptr)).dump(), v8::Null(isolate));
    }

    v8::Local<v8::Value> Npc::GetAppearanceHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        auto *npc = ResolveNpc();
        return handleScope.Escape(npc != nullptr ? v8::Local<v8::Value>(AppearanceToV8(isolate, npc->appearance)) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    v8::Local<v8::Value> Npc::GetWearingHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context       = isolate->GetCurrentContext();
        const std::vector<std::string> guids = KCDC::Features::Npc::Npcs::GetWearing(ResolveNpc());
        v8::Local<v8::Array> list            = v8::Array::New(isolate, static_cast<int>(guids.size()));
        for (std::size_t i = 0; i < guids.size(); ++i) {
            list->Set(context, static_cast<std::uint32_t>(i), v8pp::to_v8(isolate, guids[i])).Check();
        }
        return handleScope.Escape(list);
    }

    v8::Local<v8::Value> Npc::GetSimulatorHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        auto *npc = ResolveNpc();
        return handleScope.Escape(npc != nullptr ? WrapPlayer(isolate, npc->simulatorNetworkId) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    std::string Npc::ToString() const {
        std::ostringstream ss;
        ss << "Npc{ id: " << GetId() << ", name: " << GetName() << ", intent: " << GetIntent() << ", status: " << GetStatus() << " }";
        return ss.str();
    }

    v8pp::class_<Npc> &Npc::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Npc>>(isolate, ServerCatalog(), "Npc", "A server-owned NPC: spawned by a resource, simulated by whichever client is nearest.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing NPC with this ID; use Npc.create() to spawn one."))
                .function("toString", &Npc::ToString, v8pp::metadata::docs("string", {}, "Formats this NPC handle for logging and debugging.", "The NPC's ID, name, intent and last reported status."))
                .function("isNametagVisible", &Npc::GetShowNametag, v8pp::metadata::docs("boolean", {}, "Checks whether this NPC's nametag is drawn; the same switch as the `nametag` property.", "True while the nametag is shown."))
                .function("setNametagVisible", &Npc::SetShowNametag,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to draw this NPC's nametag for every player, false to hide it.")},
                        "Shows or hides the name over this NPC's head for every player; the same switch as the `nametag` property. Each player can still hide all nametags locally."))
                .function("isNametagHealthVisible", &Npc::IsNametagHealthVisible, v8pp::metadata::docs("boolean", {}, "Checks whether the health bar under this NPC's nametag is shown.", "True unless the health bar was hidden."))
                .function("setNametagHealthVisible", &Npc::SetNametagHealthVisible,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to show the health bar under this NPC's name, false to hide it.")}, "Shows or hides the health bar under this NPC's nametag, leaving the name itself alone."))
                .function("getNametagText", &Npc::GetNametagText, v8pp::metadata::docs("string", {}, "Reads this NPC's nametag text override.", "The override, or an empty string when the NPC's `name` is drawn."))
                .function("setNametagText", &Npc::SetNametagText,
                    v8pp::metadata::docs("void", {v8pp::metadata::param("text", "string", true, "Text to show instead of the NPC's name; empty or omitted restores the name.")},
                        "Overrides the text drawn on this NPC's nametag without renaming it: `name` still reaches conversation and the soul."))
                .function("getNametagColor", &Npc::GetNametagColor, v8pp::metadata::docs("number", {}, "Reads this NPC's nametag color.", "Packed 0xAARRGGBB color; opaque white when untinted."))
                .function("setNametagColor", &Npc::SetNametagColor, v8pp::metadata::docs("void", {v8pp::metadata::param("color", "number", false, "Packed 0xAARRGGBB color.")}, "Tints the text on this NPC's nametag."));

            _class->property("soul", &Npc::GetSoul, v8pp::metadata::property_docs("string", "GUID of the soul the body was spawned against, or empty for an adopted level body. Read-only: a soul decides what the body is, so a different one is a different NPC."));
            _class->property("actorClass", &Npc::GetActorClass, v8pp::metadata::property_docs("string", "Native entity class, including `NPC`, `NPC_Female`, or the matching animal class from `Npc.animals()`."));
            _class->property("levelGuid", &Npc::GetLevelGuid, v8pp::metadata::property_docs("number", "`EntityGuid` of the level body this NPC adopted, or 0 for a spawned one."));
            _class->property("name", &Npc::GetName, &Npc::SetName, v8pp::metadata::property_docs("string", "What every client shows over the body and in conversation. Empty leaves the name its soul was born with."));
            _class->property("faction", &Npc::GetFaction, &Npc::SetFaction, v8pp::metadata::property_docs("number", "Faction row used for relationship and crime decisions; 0 is no faction."));
            _class->property("health", &Npc::GetHealth, &Npc::SetHealth, v8pp::metadata::property_docs("number", "The server's ledger of this body's health, clamped to `maxHealth`. Combat damage arrives here after the server has agreed to it."));
            _class->property("maxHealth", &Npc::GetMaxHealth, v8pp::metadata::property_docs("number", "Health the body starts with and is revived to."));
            _class->property("alive", &Npc::GetAlive, v8pp::metadata::property_docs("boolean", "False once health reached zero. A dead NPC is still an entity -- it is a corpse, and it can still be looted or revived."));
            _class->property("invulnerable", &Npc::GetInvulnerable, &Npc::SetInvulnerable, v8pp::metadata::property_docs("boolean", "Whether damage is refused before it reaches the ledger."));
            _class->property("frozen", &Npc::GetFrozen, &Npc::SetFrozen, v8pp::metadata::property_docs("boolean", "Whether the body holds its pose whatever its intent says."));
            _class->property("interactable", &Npc::GetInteractable, &Npc::SetInteractable, v8pp::metadata::property_docs("boolean", "Whether clients watch the use key against this body and raise `npcInteract`. On by default."));
            _class->property("nametag", &Npc::GetShowNametag, &Npc::SetShowNametag, v8pp::metadata::property_docs("boolean", "Whether its name is drawn over it the way a player's is."));
            _class->property("lootable", &Npc::GetLootable, &Npc::SetLootable, v8pp::metadata::property_docs("boolean", "Whether its corpse keeps its inventory for whoever searches it."));
            _class->prototype_function("getAnimalLoot", &GetAnimalLoot,
                v8pp::metadata::docs("AnimalLootItem[] | null", {},
                    "Returns game-data loot candidates for this animal. Does not roll or populate stock. The gamemode chooses chances and writes Inventory.set at spawn. Null for non-animals, excluded souls or unresolved adopted bodies; an empty array is a known empty preset."));
            _class->prototype_function("getInventory", &GetNpcInventory,
                v8pp::metadata::docs("InventoryState | null", {},
                    "Reads server-owned stock and worn row counts. Inventory.get/add/remove/setProperties/set/transfer also accept Npc handles. Default native stock becomes ready at npcInventoryReady; Inventory.set may provide it before a body exists."));
            _class->property("locomotion", &Npc::GetLocomotion, &Npc::SetLocomotion,
                v8pp::metadata::property_docs("'native' | 'kinematic'", "How the client simulator moves the body. `native` (default) uses the game's movement controller and walk animations. `kinematic` moves the body directly without walk animation. "
                                                                        "Only these exact, case-sensitive strings are accepted; invalid values throw without changing the mode. "
                                                                        "Neither finds a way around anything: the game's movement controller steers straight at the point it is given. Routes come from server pathfinding, which hands either mode mesh corners -- see `moveTo`."));
            _class->property("intent", &Npc::GetIntent, v8pp::metadata::property_docs("string", "What the NPC has been told to do: `hold`, `moveTo`, `follow`, `flee`, `lookAt`, `playAnim`, `talk` or `attack`."));
            _class->property("patrolState", &Npc::GetPatrolState, v8pp::metadata::property_docs("PatrolState | null", "Current patrol snapshot and progress, or null when not patrolling. Updating a route does not change a running patrol."));
            _class->property("status", &Npc::GetStatus,
                v8pp::metadata::property_docs("string",
                    "What has become of the current order: `idle`, `running`, `reached`, `blocked` or `failed`. A move reads `running` from the moment it is given -- while its route is planned and while it walks the route's corners -- until it ends, and then the outcome `npcIntentDone` announced, which stays until the next order. "
                    "A follow reads `reached` while it is within its radius. Dormant moves in `auto` or `server` mode are carried out and decided by the server; dormant `game` moves wait for a client simulator."));

            _class->prototype_function("remove", &JS_Remove, v8pp::metadata::docs("void", {}, "Despawns this NPC everywhere, after emitting npcDestroy."));
            _class->prototype_function("hold", &JS_Hold, v8pp::metadata::docs("void", {}, "Cancels whatever it was doing and leaves it standing where it is."));
            _class->prototype_function("moveTo", &JS_MoveTo,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "Where to walk to."),
                        v8pp::metadata::param("options", kNpcMoveToOptionsType, true, "`speed` defaults to `walk`; `radius` is the arrival distance in metres (default 1.5). `pathfinding` defaults to `auto`."),
                    },
                    "Moves to `position`, cancels any patrol, and emits `npcIntentDone` once when the move ends.\n\n"
                    "The game's own movement steers a body straight at the point it is given and finds no way around anything, so routes come from the server's navigation mesh. The server plans the route and hands the simulating client one corner at a time, the next before the body reaches the last, so it walks through bends without stopping; "
                    "a dormant NPC is walked along the same route by the server. Routes only use doorways whose door stands open.\n\n"
                    "`auto` (the default) routes on the server whenever a mesh is loaded. Without one, a simulating client steers straight at `position` and a dormant NPC moves in a straight line. "
                    "`game` steers straight at `position` and waits while dormant; if the body reports `blocked` and a mesh is loaded, the server tries a route before emitting `npcIntentDone`. "
                    "`server` always routes on the server and throws without a loaded mesh, preserving the previous order.\n\n"
                    "A missing or partial route ends with `blocked`, and a blocked route is not retried. Route plans are queued and spread over ticks; the move reads `running` meanwhile.",
                    "True when the order went out; false for an invalid NPC or destination."));
            _class->prototype_function("patrol", &JS_Patrol,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("points", "(Vector3 | Partial<Vector3>)[] | PatrolRoute | string", false, "Waypoints, a named route handle, or its stable id. Named routes are copied at start."),
                        v8pp::metadata::param("options", kNpcPatrolOptionsType, true,
                            "Point arrays retain their defaults: loop, walk, zero wait. Named routes use their authored defaults. Call options override route defaults; waypoint overrides take priority. Use mode for once, loop or pingPong; do not combine mode and loop. radius is the arrival distance. pathfinding uses the moveTo modes for every leg."),
                    },
                    "Walks the waypoints in order. The server stores the route and replicates the current target. `npcIntentDone` fires once per leg, after its final mesh corner or a movement failure.\n\n"
                    "A leg that ends `blocked` or `failed` is skipped after at least 2 seconds. When every leg of a lap has failed in a row, the patrol is abandoned and that last leg reports `failed` instead.",
                    "True when the route was accepted; false for an empty route, more than 256 waypoints, a waypoint that is not finite or lies outside the world, or a `waitSeconds` that is not a number. Throws if server mode has no loaded mesh, leaving the previous order intact."));
            _class->prototype_function("attack", &JS_Attack,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("target", "Player | Npc | number", false, "The combatant to pursue and attack."), v8pp::metadata::param("options", "{ weapon?: \"melee\" | \"bow\" }", true, "Defaults to melee. Bow requires an equipped bow and compatible arrows.")},
                    "Orders native combat against a live player or NPC in the same visible world within 128 metres. Another order stops it. The order fails when its target dies, disappears or leaves that range. It waits while dormant; no offline damage is simulated. Bow attacks stop within 18 metres and use native projectiles. Equipped NPC arrows are replenished while shooting.",
                    "True when accepted. Invalid targets leave the previous order unchanged."));
            _class->prototype_function("follow", &JS_Follow,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("target", "Player | Npc | number", false, "Who to follow, as a handle or a network ID."),
                        v8pp::metadata::param("options", kNpcMoveOptionsType, true, "`radius` is how close it tries to stay, in metres; `speed` is the pace."),
                    },
                    "Keeps the NPC near somebody as they move, steering straight at them. `npcIntentDone` reports `reached` the first time it catches up, and the follow carries on. If the body reports `blocked`, a loaded mesh lets the server route to the target's current position before reporting it. After recovery it resumes following the moving target.",
                    "True when the order went out."));
            _class->prototype_function("flee", &JS_Flee,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("from", "Player | Npc | number | Vector3 | Partial<Vector3>", false, "Who or what to run away from: a player or NPC, as a handle or a network ID, or a place."),
                        v8pp::metadata::param("options", kNpcMoveOptionsType, true, "`radius` is how far away is far enough; `speed` is the pace, `run` by default. An animal picks its own pace."),
                    },
                    "Sends the NPC away from a place, or from a player or NPC; `npcIntentDone` reports `reached` once the requested distance separates them.\n\n"
                    "An animal runs the game's own escape: its own mind picks the pace and the path, and runs as far as its kind runs, so `reached` comes only when that run takes it the requested distance clear. A mind that has not taken the escape within five seconds fails the order. That mind keeps the animal until another order replaces this one, so once clear it goes on as the game's animals do; a client that takes the animal over after `reached` holds it where it stands. A dormant animal waits for a client to run it.\n\n"
                    "Anyone else steers straight away from the place, or from where the player or NPC stood when ordered, until the requested distance separates them. If that body reports `blocked`, a loaded mesh lets the server try a route to a point past that distance, on the ground at the NPC's own height, before reporting it.",
                    "True when the order went out; false for a player or NPC that is not there, or for the NPC itself."));
            _class->prototype_function("lookAt", &JS_LookAt,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("target", "Player | Npc | number | Vector3 | Partial<Vector3>", false, "Who or what to look at.")}, "Turns the NPC's head, and its body when it has to, without moving it.", "True when the order went out."));
            _class->prototype_function("teleport", &JS_Teleport,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "Where to put it."),
                        v8pp::metadata::param("rotation", "Quaternion | Vector3 | Partial<Vector3>", true, "Optional facing; a Quaternion, or Euler angles in degrees."),
                    },
                    "Moves the body outright, whoever is simulating it. Unlike a player teleport this is a write rather than a request: the server bumps the body's epoch, so a pose the simulator had already sent cannot put it back.", "True when the position was usable."));
            _class->prototype_function("playAnimation", &JS_PlayAnimation,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("fragment", "string | number", false, "A Mannequin fragment, as `Animations.list` names it -- or, as before, a row of the shipped emote catalog, which plays that gesture once and is not remembered."),
                        v8pp::metadata::param("options", kAnimationOptionsType, true,
                            "`clip` plays a clip the server streams instead of a fragment (`animations/kcdc/<resource>/wave.caf`, the fragment left `\"\"`), `tags` pick the variant, `loop` repeats it until stopped, `props` puts up to two models in its hands. `lockMovement` does nothing here: an NPC standing still is its intent's business."),
                    },
                    "Makes the NPC play one of the game's animations -- chopping wood with an axe in hand, drawing water, sitting, drinking -- on every client, the one simulating it included. It is state rather than a one-off: a client that streams the NPC in, and the next one to simulate it, play it too, until `stopAnimation` or the next `playAnimation`, which both cut it at once.\n\nGive it something to do that keeps it in place, `hold` or `lookAt`: an order to walk makes the legs fight a full-body animation. A hit or a fall can cut the animation short; `loop` starts it again, and keeps a one-shot going pass after pass with no gap.\n\n`npcAnimationEnd` fires once when the animation this request started is over, and says how.",
                    "True when the request went out; false for a dead NPC. Throws for a prop or option it cannot use."));
            _class->prototype_function("stopAnimation", &JS_StopAnimation,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("options", kStopAnimationOptionsType, true,
                        "`standUp` plays the game's own way back to standing when the animation held the body in a sitting or lying stance tag: `StandUp` or `GetUp`, in the variant that fits. Without it the body goes straight back to standing.")},
                    "Ends what `playAnimation` started at once, takes its props away and hands the body back to its intent. The stand-up `standUp` asks for is an animation of its own and raises its own `npcAnimationEnd`.", "True when the request went out."));
            _class->prototype_function("setFacialExpression", &JS_SetFacialExpression,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("fragment", "string", false,
                            "A fragment on the face's own scope: `FE_Default`, `FE_DialogueIdle` or `FE_DialogueSpeaking` for a held mood, or an `ADLG_FA_*` gesture (`ADLG_FA_Smile`, `ADLG_FA_Wink`, `ADLG_FA_Laugh`, `ADLG_FA_Surprise`...) that plays once. `\"\"` hands the face back to the game."),
                        v8pp::metadata::param("options", kFacialExpressionOptionsType, true, "`tags` pick the variant: for the `FE_*` fragments the mood, `happy`, `angry`, `sad`, `nervous`, `pensive`, `arogant` or `drunk`, as `Animations.list(\"FE_\")` spells them."),
                    },
                    "Sets the NPC's facial expression on every client, alongside whatever its body plays. It is state: a client that streams the NPC in, and the next one to simulate it, show it too, until the next call.\n\nThe expression is the eyes, brows and mood; it never moves the mouth, which is `setTalking`. A mood tag is one of the body's own tags, so it can change the mood of its idle too.",
                    "True when the request went out; false for a dead NPC. Throws for an argument it cannot use."));
            _class->prototype_function("setTalking", &JS_SetTalking,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("talking", "boolean", false, "Whether the mouth moves."),
                        v8pp::metadata::param("style", kTalkStyleType, true, "How: `neutral` (the default), `happy`, `drunk` or `chew`."),
                    },
                    "Moves the NPC's mouth as if it were talking, on every client, until told to stop. Pair it with `say` for a line of text. It is a looping talk animation, not lip-sync: it follows no words, and the game's own lip-sync wins while the NPC speaks a voice line of its own.",
                    "True when the request went out; false for a dead NPC. Throws for a style it does not know."));
            _class->prototype_function("say", &JS_Say,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("text", "string", false, "What it says, up to 256 characters.")}, "Puts a line of speech over the body on every client that can see it.", "True when the line went out."));
            _class->prototype_function("wear", &JS_Wear,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("itemClasses", "string[]", false, "Item class GUIDs in the dashed form the game's own tables spell them.")},
                    "Dresses the body in exactly these items, replacing what it had on. Existing items are reused and missing ones are added to stock. An empty list undresses it and retains its clothes as loose inventory.",
                    "True when every GUID names an item class this build has. Otherwise nothing changes, and each refused GUID is logged."));
            _class->prototype_function("setOutfit", &JS_SetOutfit,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("preset", "string", false, "Clothing preset GUID from the game's own table.")}, "Dresses the body in one of the game's own outfits.", "True when the preset is one this build has."));
            _class->prototype_function("setAppearance", &JS_SetAppearance,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("appearance", "Partial<Appearance>", false, "The parts to change; anything left out keeps what it is wearing.")},
                    "Changes the body under the clothes -- face, hair, beard and skin. Unlike a player's, this is a write rather than a request: nobody owns an NPC's body but the server.", "True when the appearance was accepted."));
            _class->prototype_function("damage", &JS_Damage,
                v8pp::metadata::docs("void",
                    {
                        v8pp::metadata::param("amount", "number", false, "Health to take off."),
                        v8pp::metadata::param("attacker", "Player | number", true, "Who did it, for the events this raises."),
                    },
                    "Takes health off the ledger, raising `npcDamage` and, if it is the last of it, `npcDeath`. Refused for an invulnerable or already-dead body."));
            _class->prototype_function("kill", &JS_Kill,
                v8pp::metadata::docs("void", {v8pp::metadata::param("attacker", "Player | number", true, "Who to credit with it.")}, "Kills it outright, through the same path damage takes -- including an invulnerable one, which is the difference between this and `damage`."));
            _class->prototype_function("revive", &JS_Revive, v8pp::metadata::docs("void", {}, "Brings a dead NPC back at full health. Every client makes a fresh body for it, because the one they have is a corpse."));
            _class->prototype_function("pin", &JS_Pin,
                v8pp::metadata::docs("void", {v8pp::metadata::param("player", "Player | number | null", false, "Whose client should run it, or null to hand it back to the election.")},
                    "Pins simulation of this NPC to one player's client, whatever the distances say. What a scripted scene wants: the actor has to be run by the machine the scene is being played to. A pinned NPC goes dormant rather than migrating when that player leaves range, and is unpinned automatically if they disconnect."));

            // Object-valued reads go through the lambda form, as Horse.rider does.
            _class->property(
                "appearance",
                [](const Npc &self, v8::Isolate *isolate) {
                    return self.GetAppearanceHandle(isolate);
                },
                v8pp::metadata::property_docs("Appearance", "The body under the clothes, as the game's own component names. Write it with `setAppearance`."));
            _class->property(
                "wearing",
                [](const Npc &self, v8::Isolate *isolate) {
                    return self.GetWearingHandle(isolate);
                },
                v8pp::metadata::property_docs("string[]", "Its worn item classes. Before default stock is established an empty list leaves the soul's own outfit; afterward empty means undressed. Write it with `wear`, `setOutfit` or `Inventory.set`."));
            _class->property(
                "simulator",
                [](const Npc &self, v8::Isolate *isolate) {
                    return self.GetSimulatorHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null", "The player whose client is currently running this NPC, or null while it is dormant. Dormant is not broken: nobody is near enough for it to matter, and the server keeps its route advancing until somebody is."));
        }
        return *_class;
    }

    void Npc::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Npc::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("npcSpawn", "[npc: Npc]", "Dispatched immediately after an NPC is spawned or adopted, whether by `Npc.create`, a command, or anything else.");
        events.add_property("npcInventoryReady", "[npc: Npc]", "Its initial inventory has been established. The default stock is censused once by its elected simulator; Inventory.set establishes authored stock immediately.");
        events.add_property("npcHarvested", "[npc: Npc, player: Player]",
            "An accepted butchering completion. The prepopulated stock is now accessible and every client applies the butchered appearance. Emitted once per life; save Inventory.get(npc) here too to persist the harvested flag.");
        events.add_property("npcInventoryChanged", "[npc: Npc, change: InventoryChange]", "Committed stock changes, including loot transfers. Save this stock and restore it with Inventory.set. Harvest completion is reported by npcHarvested.");
        events.add_property("npcDestroy", "[npc: Npc]", "Dispatched while an NPC is being despawned. The handle still resolves, so what it was and where it stood can be read one last time.");
        events.add_property("npcIntentDone", "[npc: Npc, status: string]",
            "Dispatched once when an NPC finishes what it was told to do. `status` is `reached` when it arrived, `blocked` when it could not make progress, or `failed` when the order could not be carried out at all, or when a patrol gives up. Server-planned intermediate corners do not raise this event, and neither does a change of simulator: a finished order is not reported twice. "
            "A follow, which never finishes, raises `reached` the first time it catches up and again only after a change of simulator. A patrol steps on this event, so a handler that re-orders the NPC here replaces the route rather than racing it.");
        events.add_property("npcDamage", "[npc: Npc, attacker: Player | Npc | null, amount: number]",
            "Dispatched after health came off the ledger. For melee, the victim's simulator resolves the native hit and the server checks the accepted swing before changing health. `amount` is what was actually taken.");
        events.add_property("npcDeath", "[npc: Npc, attacker: Player | Npc | null]", "Dispatched when the last of an NPC's health goes. The body stays as a corpse and the handle keeps resolving.");
        events.add_property("npcRevive", "[npc: Npc]", "Dispatched when a dead NPC is brought back. Every client makes a new body for it.");
        events.add_property("npcAnimationEnd", std::string("[npc: Npc, fragment: string, reason: ") + kAnimationEndReasonType + "]",
            "Dispatched once when the animation a `playAnimation` request started is over; `fragment` is the request's. `reason` is `finished`, `interrupted`, `refused`, `stopped` or `replaced`, as for `playerAnimationEnd`. The first three come from the client simulating the NPC. One that nobody simulates plays nothing, so its one-shot ends `refused` as soon as it is asked for, and one cut off by a change of simulator ends `interrupted`: the new simulator does not replay what it did not see start.");
        events.add_property("npcInteract", "[npc: Npc, player: Player]",
            "Dispatched when a player presses use on an NPC whose `interactable` is on. The client reports the press and the server confirms the distance against the position it already replicates, so a claim it does not agree with never reaches here.");
        events.add_property("npcSimulatorChange", "[npc: Npc, player: Player | null]",
            "Dispatched when the client running an NPC changes -- somebody walked into range, out of it, or disconnected. `player` is null when it went dormant. Nothing about the NPC changes with it: intent, ledger and identity are the server's, and the new simulator re-derives from them.");

        v8pp::class_<Npc> &cls = GetClass(isolate);

        cls.static_function("create", &JS_Create,
            v8pp::metadata::docs("Npc", {v8pp::metadata::param("options", kNpcCreateOptionsType, false, "`soul` is a role from `Npc.roles()`, an animal name from `Npc.animals()`, or a soul GUID; `position` is where to put it. Everything else has a default.")},
                "Spawns an NPC and replicates it. Omitted or undefined `locomotion` defaults to `native`; other values must be exactly `native` or `kinematic`, otherwise it throws before spawning. "
                "It exists on the server from this moment: every client near enough makes a body for it, one of them is elected to run it, and the rest draw what that one reports.\n\n"
                "The body is not simulated until somebody is close enough to run it, which is not a failure -- a guard on the other side of the map has nothing to do that anybody can see. Read `simulator` to tell.",
                "The new NPC's handle."));
        cls.static_function("adopt", &JS_Adopt,
            v8pp::metadata::docs("Npc",
                {
                    v8pp::metadata::param("levelGuid", "number", false, "`EntityGuid` of the body the level already placed."),
                    v8pp::metadata::param("options", kNpcCreateOptionsType, true, "The same options a spawn takes; `soul` and `class` are ignored, since the body already exists."),
                },
                "Takes over one of the level's own NPCs instead of spawning a new body. Locomotion is validated as for `create`. "
                "A level `EntityGuid` is the same number on every machine, so every client finds the same body -- which is how doors, gates and stashes are already addressed. "
                "Adopting the same guid twice returns the NPC that already has it without applying the options again.",
                "The NPC handle for that body."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Npc[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every server-owned NPC.", "One handle per live NPC, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Npc | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks an NPC up by its network entity ID.", "The NPC's handle, or null when no live NPC has that ID."));
        cls.static_function("removeAll", &JS_RemoveAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Despawns NPCs, emitting npcDestroy for each.", "How many were despawned."));
        cls.static_function("roles", &JS_Roles,
            v8pp::metadata::docs("string[]", {},
                "The named kinds of NPC this build ships -- `guard`, `bandit`, `townswoman` and the rest. Each is a real soul out of the game's own tables, so a role spawns a body that already looks the part. Animal names are listed separately by `Npc.animals()`. Other values are taken as soul GUIDs.",
                "The role names."));
        cls.static_function("placements", &Placements, v8pp::metadata::docs("{ name: string; guid: string; level: string; layer: string; soul: string; actorClass: string; position: { x: number; y: number; z: number }; rotation: { w: number; x: number; y: number; z: number } }[]", {}, "Authored human NPC placements in the configured level with matching catalog souls. These are initial placements, not current NPC schedules or proof that a quest layer is loaded. Does not spawn or adopt anything."));
        cls.static_function("animals", &JS_Animals, v8pp::metadata::docs("{ name: string; soul: string; actorClass: string }[]", {}, "Animal souls with matching native classes. Pass a name or soul to Npc.create. Chickens use flock entities and are excluded."));
        cls.static_function("animalSpawnpoints", &JS_AnimalSpawnpoints,
            v8pp::metadata::docs("{ level: string; guid: string; name: string; layer: string; soul: string; actorClass: string; position: Vector3; count: number; respawnDays: number; spawnInFlock: boolean; spawnAreaGuid: string }[]",
                {v8pp::metadata::param("level", "string", true, "Defaults to the running server's level. Pass * for all levels.")},
                "Level-authored animal spawners. Positions are markers and must be projected onto navigation. GUIDs are hex strings. Counts and respawn days describe the original game; scripts decide their population and respawn policy."));
        cls.publish(global);
    }

    void Events::NpcSpawned(std::uint64_t npcNetworkId) {
        EmitNpcEvent("npcSpawn", npcNetworkId);
    }

    void Events::NpcDestroyed(std::uint64_t npcNetworkId) {
        EmitNpcEvent("npcDestroy", npcNetworkId);
    }

    void Events::NpcRevived(std::uint64_t npcNetworkId) {
        EmitNpcEvent("npcRevive", npcNetworkId);
    }

    void Events::NpcIntentFinished(std::uint64_t npcNetworkId, const char *status) {
        const std::string text = status != nullptr ? status : "";
        EmitReservedEvent("npcIntentDone", [npcNetworkId, text](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(v8pp::to_v8(isolate, text));
            return args;
        });
    }

    void Events::NpcDamaged(std::uint64_t npcNetworkId, std::uint64_t attackerNetworkId, float amount) {
        EmitReservedEvent("npcDamage", [npcNetworkId, attackerNetworkId, amount](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(WrapCombatant(isolate, attackerNetworkId));
            args.push_back(v8pp::to_v8(isolate, static_cast<double>(amount)));
            return args;
        });
    }

    void Events::NpcDied(std::uint64_t npcNetworkId, std::uint64_t attackerNetworkId) {
        EmitReservedEvent("npcDeath", [npcNetworkId, attackerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(WrapCombatant(isolate, attackerNetworkId));
            return args;
        });
    }

    void Events::NpcAnimationEnded(std::uint64_t npcNetworkId, const std::string &fragment, Shared::Game::AnimationEndReason reason) {
        EmitReservedEvent("npcAnimationEnd", [npcNetworkId, &fragment, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(v8pp::to_v8(isolate, fragment));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::AnimationEndReasonName(reason))));
            return args;
        });
    }

    void Events::NpcInteracted(std::uint64_t npcNetworkId, std::uint64_t playerNetworkId) {
        EmitReservedEvent("npcInteract", [npcNetworkId, playerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        });
    }

    void Events::NpcSimulatorChanged(std::uint64_t npcNetworkId, std::uint64_t playerNetworkId) {
        EmitReservedEvent("npcSimulatorChange", [npcNetworkId, playerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapNpc(isolate, npcNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        });
    }
} // namespace KCDC::Scripting
