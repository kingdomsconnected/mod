#include "area_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/gathering/gathering_scripting.h"

#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/area_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/option_field.h"
#include "shared/scripting/rotation_arg.h"
#include "shared/scripting/virtual_world_arg.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/builtins/entity.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>

namespace KCDC::Scripting {

    namespace {
        // Unguarded on purpose: PostInit installs the area service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::Area::AreaService &ResolveAreas() {
            return Server::Get()->GetAreas();
        }

        // The handle a prototype function was called on, or nullptr.
        Area *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Area>::unwrap_object(info.GetIsolate(), info.This());
        }

        v8::Local<v8::Value> WrapOccupant(v8::Isolate *isolate, std::uint64_t networkId, KCDC::Features::Area::AreaOccupantKind kind) {
            switch (kind) {
            case KCDC::Features::Area::AreaOccupantKind::Player: return WrapPlayer(isolate, networkId);
            case KCDC::Features::Area::AreaOccupantKind::Horse: return WrapHorse(isolate, networkId);
            case KCDC::Features::Area::AreaOccupantKind::Cart: return WrapCart(isolate, networkId);
            case KCDC::Features::Area::AreaOccupantKind::Npc: return WrapNpc(isolate, networkId);
            }
            return v8::Null(isolate);
        }

        void SetProperty(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key, v8::Local<v8::Value> value) {
            object->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key), value).Check();
        }

        bool ReadVector(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out) {
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }
            const glm::vec3 read = v8pp::convert<glm::vec3>::from_v8(isolate, value);
            if (!std::isfinite(read.x) || !std::isfinite(read.y) || !std::isfinite(read.z)) {
                return false;
            }
            out = read;
            return true;
        }

        // A plain {x, y, z, w} reads as a quaternion, so what `toJSON` wrote comes back unchanged; anything else goes the way every other verb reads a rotation.
        glm::quat ReadRotation(v8::Isolate *isolate, v8::Local<v8::Value> value, const glm::quat &fallback) {
            if (value.IsEmpty() || !value->IsObject()) {
                return fallback;
            }
            if (!OptionField(isolate, value.As<v8::Object>(), "w").IsEmpty()) {
                const glm::quat read = v8pp::convert<glm::quat>::from_v8(isolate, value);
                return glm::dot(read, read) > 0.0F ? glm::normalize(read) : fallback;
            }
            return ParseRotationArg(isolate, value, fallback);
        }

        std::vector<std::string> ReadLabels(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            std::vector<std::string> labels;
            if (value.IsEmpty() || !value->IsArray()) {
                return labels;
            }
            v8::Local<v8::Array> array     = value.As<v8::Array>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            for (std::uint32_t index = 0; index < array->Length(); ++index) {
                v8::Local<v8::Value> entry;
                if (array->Get(context, index).ToLocal(&entry) && entry->IsString()) {
                    std::string label = v8pp::from_v8<std::string>(isolate, entry);
                    if (!label.empty()) {
                        labels.push_back(std::move(label));
                    }
                }
            }
            return labels;
        }

        // The metadata as JSON text, or empty when it is not a plain object JSON can hold.
        std::string StringifyMetadata(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            if (value.IsEmpty() || !value->IsObject() || value->IsArray() || value->IsFunction()) {
                return {};
            }
            return StringifyValue(isolate, value).value_or(std::string());
        }

        /**
         * Reads a shape definition over `shape`: a field left out keeps what
         * `shape` already holds -- the area's current shape for `setShape`,
         * the defaults for `create`. False, with `error`, for a type the
         * engine has no shape for or a malformed point.
         */
        bool ReadShape(v8::Isolate *isolate, v8::Local<v8::Object> definition, KCDC::Shared::Game::AreaShape &shape, std::string &error) {
            const v8::Local<v8::Value> type = OptionField(isolate, definition, "type");
            if (!type.IsEmpty()) {
                if (!type->IsString() || !KCDC::Shared::Game::ParseAreaShapeType(v8pp::from_v8<std::string>(isolate, type), shape.type)) {
                    error = "type must be 'box', 'shape' or 'sphere'";
                    return false;
                }
            }

            ReadVector(isolate, OptionField(isolate, definition, "position"), shape.position);
            shape.rotation = ReadRotation(isolate, OptionField(isolate, definition, "rotation"), shape.rotation);
            ReadVector(isolate, OptionField(isolate, definition, "min"), shape.boxMin);
            ReadVector(isolate, OptionField(isolate, definition, "max"), shape.boxMax);
            FloatOption(isolate, definition, "height", shape.height);
            FloatOption(isolate, definition, "radius", shape.radius);

            const v8::Local<v8::Value> points = OptionField(isolate, definition, "points");
            if (!points.IsEmpty()) {
                if (!points->IsArray()) {
                    error = "points must be an array of {x, y, z}";
                    return false;
                }
                v8::Local<v8::Array> array     = points.As<v8::Array>();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                shape.points.clear();
                for (std::uint32_t index = 0; index < array->Length(); ++index) {
                    v8::Local<v8::Value> entry;
                    glm::vec3 point {0.0F};
                    if (!array->Get(context, index).ToLocal(&entry) || !ReadVector(isolate, entry, point)) {
                        error = "points[" + std::to_string(index) + "] is not a {x, y, z}";
                        return false;
                    }
                    shape.points.push_back(point);
                }
            }
            return true;
        }

        // The pose and world of any entity handle, or false for one that has gone or has not reported a pose yet.
        bool EntityPose(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &position, MafiaNet::VirtualWorldId &virtualWorld) {
            auto *handle      = v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, value);
            auto *replication = Framework::CoreModules::GetReplication();
            auto *entity      = handle != nullptr && replication != nullptr ? replication->GetEntityByNetworkID(handle->GetId()) : nullptr;
            if (entity == nullptr) {
                return false;
            }
            // A body sits at the origin until its owner reports a pose.
            if (auto *human = replication->GetEntity<Shared::Entities::HumanEntity>(handle->GetId()); human != nullptr && !human->transformReady) {
                return false;
            }
            if (auto *npc = replication->GetEntity<Shared::Entities::NpcEntity>(handle->GetId()); npc != nullptr && !npc->transformReady) {
                return false;
            }
            position     = entity->position;
            virtualWorld = entity->GetVirtualWorld();
            return true;
        }

        // JS: Area.create(definition) -> Area
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: expected (definition)"));
                return;
            }
            v8::Local<v8::Object> source = info[0].As<v8::Object>();

            const v8::Local<v8::Value> id = OptionField(isolate, source, "id");
            if (id.IsEmpty() || !id->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: definition.id must be a string"));
                return;
            }
            if (OptionField(isolate, source, "type").IsEmpty()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: definition.type must be 'box', 'shape' or 'sphere'"));
                return;
            }

            // Read off the live object rather than through JSON: a Vector3 or Quaternion handle has no own properties to stringify.
            KCDC::Shared::Game::AreaDefinition definition;
            definition.id = v8pp::from_v8<std::string>(isolate, id);
            std::string error;
            if (!ReadShape(isolate, source, definition.shape, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: " + error));
                return;
            }

            if (const v8::Local<v8::Value> name = OptionField(isolate, source, "name"); !name.IsEmpty() && name->IsString()) {
                definition.name = v8pp::from_v8<std::string>(isolate, name);
            }
            definition.labels = ReadLabels(isolate, OptionField(isolate, source, "labels"));
            if (const v8::Local<v8::Value> metadata = OptionField(isolate, source, "metadata"); !metadata.IsEmpty()) {
                definition.metadataJson = StringifyMetadata(isolate, metadata);
                if (definition.metadataJson.empty()) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: definition.metadata must be a plain object"));
                    return;
                }
            }
            if (const v8::Local<v8::Value> world = OptionField(isolate, source, "virtualWorld"); !world.IsEmpty()) {
                definition.virtualWorld = world->IsNumber() ? VirtualWorldFromNumber(v8pp::from_v8<double>(isolate, world)) : std::nullopt;
                if (!definition.virtualWorld.has_value()) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: definition.virtualWorld must be a whole number from 0 to 4294967295"));
                    return;
                }
            }
            if (const v8::Local<v8::Value> enabled = OptionField(isolate, source, "enabled"); !enabled.IsEmpty() && enabled->IsBoolean()) {
                definition.enabled = enabled->BooleanValue(isolate);
            }

            if (const auto collision = OptionField(isolate, source, "collision"); !collision.IsEmpty()) {
                if (!collision->IsBoolean()) {
                    isolate->ThrowError("Area.create: collision must be a boolean");
                    return;
                }
                definition.collision = collision->BooleanValue(isolate);
            }
            for (const auto &[key, plate] : {std::pair {"collisionFloor", &definition.collisionOptions.floor}, std::pair {"collisionCeiling", &definition.collisionOptions.ceiling}}) {
                if (const auto value = OptionField(isolate, source, key); !value.IsEmpty()) {
                    if (!value->IsBoolean()) {
                        isolate->ThrowError(v8pp::to_v8(isolate, std::string("Area.create: ") + key + " must be a boolean"));
                        return;
                    }
                    *plate = value->BooleanValue(isolate);
                }
            }
            if (const auto thickness = OptionField(isolate, source, "collisionThickness"); !thickness.IsEmpty()) {
                if (!thickness->IsNumber()) {
                    isolate->ThrowError("Area.create: collisionThickness must be a number");
                    return;
                }
                definition.collisionOptions.thickness = static_cast<float>(v8pp::from_v8<double>(isolate, thickness));
                if (!definition.collisionOptions.IsValid()) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: " + KCDC::Shared::Game::AreaCollisionOptions::ThicknessRangeError()));
                    return;
                }
            }
            auto *area = ResolveAreas().Create(std::move(definition), error);
            if (area == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.create: " + error));
                return;
            }
            info.GetReturnValue().Set(WrapArea(isolate, area->handle));
        }

        // JS: Area.getById(id) -> Area | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.getById: expected (id)"));
                return;
            }
            auto *area = ResolveAreas().FindById(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(area != nullptr && !area->destroying ? WrapArea(isolate, area->handle) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Area.getAt(position, virtualWorld?) -> Area[]
        void JS_GetAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            glm::vec3 position {0.0F};
            if (info.Length() < 1 || !ReadVector(isolate, info[0], position)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.getAt: expected (position, virtualWorld?)"));
                return;
            }
            info.GetReturnValue().Set(AreasToArray(isolate, ResolveAreas().At(position, VirtualWorldArg(info, 1))));
        }

        // JS: Area.getAtPlayer(player) -> Area[]
        void JS_GetAtPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            glm::vec3 position {0.0F};
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            if (info.Length() < 1 || !EntityPose(isolate, info[0], position, virtualWorld)) {
                info.GetReturnValue().Set(v8::Array::New(isolate));
                return;
            }
            info.GetReturnValue().Set(AreasToArray(isolate, ResolveAreas().At(position, virtualWorld)));
        }

        // JS: Area.nearby(position, radius, virtualWorld?) -> Area[]
        void JS_Nearby(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            glm::vec3 position {0.0F};
            if (info.Length() < 2 || !ReadVector(isolate, info[0], position) || !info[1]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.nearby: expected (position, radius, virtualWorld?)"));
                return;
            }
            const float radius = static_cast<float>(v8pp::from_v8<double>(isolate, info[1]));
            info.GetReturnValue().Set(AreasToArray(isolate, ResolveAreas().Nearby(position, std::max(radius, 0.0F), VirtualWorldArg(info, 2))));
        }

        // JS: Area.withLabel(label) -> Area[]
        void JS_WithLabel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Area.withLabel: expected (label)"));
                return;
            }
            info.GetReturnValue().Set(AreasToArray(isolate, ResolveAreas().WithLabel(v8pp::from_v8<std::string>(isolate, info[0]))));
        }

        // JS: Area.all(kind?) -> Area[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            std::optional<bool> level;
            if (info.Length() >= 1 && info[0]->IsString()) {
                const std::string kind = v8pp::from_v8<std::string>(isolate, info[0]);
                if (kind != "level" && kind != "script") {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Area.all: kind must be 'level' or 'script'"));
                    return;
                }
                level = kind == "level";
            }
            info.GetReturnValue().Set(AreasToArray(isolate, ResolveAreas().All(level)));
        }

        // JS: area.contains(position) -> boolean
        void JS_Contains(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            auto *area           = self != nullptr ? self->Resolve() : nullptr;
            glm::vec3 position {0.0F};
            info.GetReturnValue().Set(area != nullptr && info.Length() >= 1 && ReadVector(isolate, info[0], position) && ResolveAreas().Contains(*area, position));
        }

        bool ReadHerbArea(const v8::FunctionCallbackInfo<v8::Value> &info, KCDC::Features::Area::AreaRecord *&area, std::uint32_t &world) {
            auto *self = Self(info);
            area       = self != nullptr ? self->Resolve() : nullptr;
            world      = area != nullptr ? area->definition.virtualWorld.value_or(MafiaNet::VIRTUAL_WORLD_DEFAULT) : MafiaNet::VIRTUAL_WORLD_DEFAULT;
            if (world == MafiaNet::VIRTUAL_WORLD_GLOBAL) {
                world = MafiaNet::VIRTUAL_WORLD_DEFAULT;
            }
            if (info.Length() > 1) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Area herb methods: expected (virtualWorld?)"));
                return false;
            }
            if (info.Length() == 1 && !info[0]->IsUndefined()) {
                const auto parsed = info[0]->IsNumber() ? VirtualWorldFromNumber(v8pp::from_v8<double>(info.GetIsolate(), info[0])) : std::nullopt;
                if (!parsed) {
                    info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Area herb methods: virtualWorld must be a whole-number uint32 world ID"));
                    return false;
                }
                world = *parsed;
            }
            return true;
        }

        void JS_GetHerbs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::uint32_t world;
            KCDC::Features::Area::AreaRecord *area;
            if (!ReadHerbArea(info, area, world)) {
                return;
            }
            const auto herbs = area != nullptr ? Server::Get()->GetGathering().GetHerbs(*area, world) : std::vector<KCDC::Shared::Game::GatheringSource> {};
            info.GetReturnValue().Set(HerbsToArray(isolate, herbs));
        }

        void JS_RemoveHerbs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::uint32_t world;
            KCDC::Features::Area::AreaRecord *area;
            if (!ReadHerbArea(info, area, world)) {
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(area != nullptr ? Server::Get()->GetGathering().RemoveHerbs(*area, world) : 0));
        }

        void JS_RespawnHerbs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::uint32_t world;
            KCDC::Features::Area::AreaRecord *area;
            if (!ReadHerbArea(info, area, world)) {
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(area != nullptr ? Server::Get()->GetGathering().RespawnHerbs(*area, world) : 0));
        }

        // JS: area.containsPlayer(player) -> boolean
        void JS_ContainsPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            auto *area           = self != nullptr ? self->Resolve() : nullptr;
            glm::vec3 position {0.0F};
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            if (area == nullptr || info.Length() < 1 || !EntityPose(isolate, info[0], position, virtualWorld)) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Area::AreaService::SharesWorld(*area, virtualWorld) && ResolveAreas().Contains(*area, position));
        }

        // JS: area.setShape(definition) -> boolean
        void JS_SetShape(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self = Self(info);
            auto *area = self != nullptr ? self->Resolve() : nullptr;
            if (area == nullptr || area->IsLevel()) {
                info.GetReturnValue().Set(false);
                return;
            }
            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "area.setShape: expected (definition)"));
                return;
            }
            KCDC::Shared::Game::AreaShape shape = area->definition.shape;
            std::string error;
            if (!ReadShape(isolate, info[0].As<v8::Object>(), shape, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "area.setShape: " + error));
                return;
            }
            info.GetReturnValue().Set(ResolveAreas().SetShape(*area, shape));
        }

        // JS: area.destroy() -> boolean
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && ResolveAreas().Destroy(self->GetHandle()));
        }

        // JS: area.toJSON() -> AreaDefinition | null
        void JS_ToJSON(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self = Self(info);
            auto *area = self != nullptr ? self->Resolve() : nullptr;
            if (area == nullptr || area->IsLevel()) {
                info.GetReturnValue().SetNull();
                return;
            }
            // The shared writer, so what a script saves is exactly what the World Builder exports.
            info.GetReturnValue().Set(ParseJsonValue(isolate, KCDC::Shared::Game::AreaDefinitionToJson(area->definition).dump(), v8::Null(isolate)));
        }
    } // namespace

    v8::Local<v8::Value> WrapArea(v8::Isolate *isolate, std::uint32_t handle) {
        const auto *area = ResolveAreas().Find(handle);
        if (area == nullptr) {
            return v8::Null(isolate);
        }
        Area::GetClass(isolate);
        return v8pp::class_<Area>::create_object(isolate, handle);
    }

    v8::Local<v8::Value> AreasToArray(v8::Isolate *isolate, const std::vector<KCDC::Features::Area::AreaRecord *> &areas) {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> array     = v8::Array::New(isolate, static_cast<int>(areas.size()));
        for (std::size_t index = 0; index < areas.size(); ++index) {
            array->Set(context, static_cast<std::uint32_t>(index), WrapArea(isolate, areas[index]->handle)).Check();
        }
        return array;
    }

    v8::Local<v8::Value> AreaDoorsToArray(v8::Isolate *isolate, const std::vector<KCDC::Features::Area::AreaDoor> &doors) {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> array     = v8::Array::New(isolate, static_cast<int>(doors.size()));
        for (std::size_t index = 0; index < doors.size(); ++index) {
            v8::Local<v8::Object> door = v8::Object::New(isolate);
            SetProperty(isolate, door, "guid", v8pp::to_v8(isolate, FormatLevelGuid(doors[index].doorGuid)));
            SetProperty(isolate, door, "name", v8pp::to_v8(isolate, doors[index].name));
            SetProperty(isolate, door, "position", v8pp::to_v8(isolate, doors[index].position));
            SetProperty(isolate, door, "link", v8pp::to_v8(isolate, KCDC::Shared::Game::AreaDoorLinkName(doors[index].link)));
            array->Set(context, static_cast<std::uint32_t>(index), door).Check();
        }
        return array;
    }

    // An area being destroyed still resolves, so the handlers of its last `areaExit` can read what it was; `exists` is already false.
    KCDC::Features::Area::AreaRecord *Area::Resolve() const {
        return ResolveAreas().Find(_handle);
    }

    bool Area::Exists() const {
        const auto *area = Resolve();
        return area != nullptr && !area->destroying;
    }

    std::string Area::GetId() const {
        const auto *area = Resolve();
        return area != nullptr ? area->definition.id : std::string {};
    }

    std::string Area::GetKind() const {
        const auto *area = Resolve();
        return area == nullptr ? std::string {} : area->IsLevel() ? "level" : "script";
    }

    std::string Area::GetType() const {
        const auto *area = Resolve();
        if (area == nullptr) {
            return {};
        }
        return area->IsLevel() ? KCDC::Shared::Game::LevelAreaClassName(area->level->areaClass) : KCDC::Shared::Game::AreaShapeTypeName(area->definition.shape.type);
    }

    std::string Area::GetName() const {
        const auto *area = Resolve();
        return area != nullptr ? area->definition.name : std::string {};
    }

    void Area::SetName(const std::string &name) {
        if (auto *area = Resolve(); area != nullptr && !area->IsLevel() && !name.empty()) {
            area->definition.name = name;
        }
    }

    std::vector<std::string> Area::GetLabels() const {
        const auto *area = Resolve();
        return area != nullptr ? area->definition.labels : std::vector<std::string> {};
    }

    void Area::SetLabels(const std::vector<std::string> &labels) {
        if (auto *area = Resolve(); area != nullptr && !area->IsLevel()) {
            area->definition.labels.clear();
            for (const std::string &label : labels) {
                if (!label.empty()) {
                    area->definition.labels.push_back(label);
                }
            }
        }
    }

    v8::Local<v8::Value> Area::GetMetadata(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        return ParseJsonValue(isolate, area != nullptr ? area->definition.metadataJson : std::string("{}"), v8::Object::New(isolate));
    }

    void Area::SetMetadata(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *area = Resolve();
        if (area == nullptr || area->IsLevel()) {
            return;
        }
        std::string json = StringifyMetadata(isolate, value);
        if (json.empty()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "area.metadata must be a plain object"));
            return;
        }
        area->definition.metadataJson = std::move(json);
    }

    bool Area::HasCollision() const {
        const auto *area = Resolve();
        return area != nullptr && area->definition.collision;
    }

    void Area::SetCollision(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsBoolean()) {
            isolate->ThrowError("area.collision must be a boolean");
            return;
        }
        if (auto *area = Resolve()) {
            std::string error;
            if (!ResolveAreas().SetCollision(*area, value->BooleanValue(isolate), error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, error));
            }
        }
    }

    bool Area::HasCollisionFloor() const {
        return GetCollisionOptions().floor;
    }

    void Area::SetCollisionFloor(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsBoolean()) {
            isolate->ThrowError("area.collisionFloor must be a boolean");
            return;
        }
        auto options  = GetCollisionOptions();
        options.floor = value->BooleanValue(isolate);
        ApplyCollisionOptions(isolate, options);
    }

    bool Area::HasCollisionCeiling() const {
        return GetCollisionOptions().ceiling;
    }

    void Area::SetCollisionCeiling(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsBoolean()) {
            isolate->ThrowError("area.collisionCeiling must be a boolean");
            return;
        }
        auto options    = GetCollisionOptions();
        options.ceiling = value->BooleanValue(isolate);
        ApplyCollisionOptions(isolate, options);
    }

    double Area::GetCollisionThickness() const {
        return GetCollisionOptions().thickness;
    }

    void Area::SetCollisionThickness(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsNumber()) {
            isolate->ThrowError("area.collisionThickness must be a number");
            return;
        }
        auto options      = GetCollisionOptions();
        options.thickness = static_cast<float>(v8pp::from_v8<double>(isolate, value));
        ApplyCollisionOptions(isolate, options);
    }

    KCDC::Shared::Game::AreaCollisionOptions Area::GetCollisionOptions() const {
        const auto *area = Resolve();
        return area != nullptr ? area->definition.collisionOptions : KCDC::Shared::Game::AreaCollisionOptions {};
    }

    void Area::ApplyCollisionOptions(v8::Isolate *isolate, const KCDC::Shared::Game::AreaCollisionOptions &options) {
        if (auto *area = Resolve()) {
            std::string error;
            if (!ResolveAreas().SetCollisionOptions(*area, options, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, error));
            }
        }
    }

    bool Area::IsEnabled() const {
        const auto *area = Resolve();
        return area != nullptr && area->definition.enabled;
    }

    void Area::SetEnabled(bool enabled) {
        if (auto *area = Resolve()) {
            ResolveAreas().SetEnabled(*area, enabled);
        }
    }

    v8::Local<v8::Value> Area::GetVirtualWorld(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        if (area == nullptr || !area->definition.virtualWorld.has_value()) {
            return v8::Null(isolate);
        }
        return v8pp::to_v8(isolate, static_cast<double>(*area->definition.virtualWorld));
    }

    void Area::SetVirtualWorld(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *area = Resolve();
        if (area == nullptr || area->IsLevel()) {
            return;
        }
        if (value->IsNullOrUndefined()) {
            ResolveAreas().SetVirtualWorld(*area, std::nullopt);
            return;
        }
        const std::optional<MafiaNet::VirtualWorldId> world = value->IsNumber() ? VirtualWorldFromNumber(v8pp::from_v8<double>(isolate, value)) : std::nullopt;
        if (!world.has_value()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "area.virtualWorld must be null or a whole number from 0 to 4294967295"));
            return;
        }
        ResolveAreas().SetVirtualWorld(*area, world);
    }

    v8::Local<v8::Value> Area::GetPosition(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        if (area == nullptr) {
            return v8::Null(isolate);
        }
        // A level area has no pose of its own; see LevelAreaCentre.
        return v8pp::to_v8(isolate, area->IsLevel() ? KCDC::Shared::Game::LevelAreaCentre(*area->level) : area->definition.shape.position);
    }

    void Area::SetPosition(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *area = Resolve();
        glm::vec3 position {0.0F};
        if (area == nullptr || area->IsLevel() || !ReadVector(isolate, value, position)) {
            return;
        }
        KCDC::Shared::Game::AreaShape shape = area->definition.shape;
        shape.position                      = position;
        ResolveAreas().SetShape(*area, shape);
    }

    v8::Local<v8::Value> Area::GetRotation(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        if (area == nullptr) {
            return v8::Null(isolate);
        }
        return v8pp::to_v8(isolate, area->IsLevel() ? glm::quat(1.0F, 0.0F, 0.0F, 0.0F) : area->definition.shape.rotation);
    }

    void Area::SetRotation(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *area = Resolve();
        if (area == nullptr || area->IsLevel()) {
            return;
        }
        KCDC::Shared::Game::AreaShape shape = area->definition.shape;
        shape.rotation                      = ReadRotation(isolate, value, shape.rotation);
        ResolveAreas().SetShape(*area, shape);
    }

    float Area::GetHeight() const {
        const auto *area = Resolve();
        if (area == nullptr) {
            return 0.0F;
        }
        return area->IsLevel() ? area->level->height : area->definition.shape.height;
    }

    std::string Area::GetLocation() const {
        const auto *area = Resolve();
        return area != nullptr && area->IsLevel() ? area->level->location : std::string {};
    }

    v8::Local<v8::Value> Area::GetBounds(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        if (area == nullptr) {
            return v8::Null(isolate);
        }
        v8::Local<v8::Object> bounds = v8::Object::New(isolate);
        SetProperty(isolate, bounds, "min", v8pp::to_v8(isolate, area->boundsMin));
        SetProperty(isolate, bounds, "max", v8pp::to_v8(isolate, area->boundsMax));
        return bounds;
    }

    v8::Local<v8::Value> Area::GetMembers(v8::Isolate *isolate) const {
        std::vector<KCDC::Features::Area::AreaRecord *> members;
        if (const auto *area = Resolve()) {
            for (const std::uint32_t handle : area->members) {
                if (auto *member = ResolveAreas().Find(handle)) {
                    members.push_back(member);
                }
            }
        }
        return AreasToArray(isolate, members);
    }

    v8::Local<v8::Value> Area::GetDoors(v8::Isolate *isolate) const {
        const auto *area = Resolve();
        return AreaDoorsToArray(isolate, area != nullptr ? ResolveAreas().DoorsOf(*area) : std::vector<KCDC::Features::Area::AreaDoor> {});
    }

    v8::Local<v8::Value> Area::GetPlayers(v8::Isolate *isolate) const {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> players   = v8::Array::New(isolate);
        std::uint32_t index            = 0;
        if (const auto *area = Resolve()) {
            for (const auto &occupant : area->occupants) {
                if (occupant.second == KCDC::Features::Area::AreaOccupantKind::Player) {
                    players->Set(context, index++, WrapPlayer(isolate, occupant.first)).Check();
                }
            }
        }
        return players;
    }

    v8::Local<v8::Value> Area::GetOccupants(v8::Isolate *isolate) const {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> occupants = v8::Array::New(isolate);
        std::uint32_t index            = 0;
        if (const auto *area = Resolve()) {
            for (const auto &occupant : area->occupants) {
                occupants->Set(context, index++, WrapOccupant(isolate, occupant.first, occupant.second)).Check();
            }
        }
        return occupants;
    }

    std::string Area::ToString() const {
        std::ostringstream ss;
        ss << "Area{ id: " << GetId() << ", kind: " << GetKind() << ", type: " << GetType() << ", name: " << GetName() << " }";
        return ss.str();
    }

    v8pp::class_<Area> &Area::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            _class =
                std::make_unique<v8pp::class_<Area>>(isolate, ServerCatalog(), "Area", "Handle for one area: one of the level's own gameplay areas, or one a script or the World Builder drew. The server tests who is inside itself, by the game's own rule for that kind of area.");
            _class->auto_wrap_objects(true);
            _class->ctor<std::uint32_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("handle", "number", false, "Internal area handle.")}, "Wraps an area handle the server already holds; use Area.create, Area.getById or the lookups to get one."))
                .function("toString", &Area::ToString, v8pp::metadata::docs("string", {}, "Formats this area for logging and debugging.", "Its id, kind, type and name."));

            _class->prototype_function("contains", &JS_Contains,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("position", "Vector3", false, "The world position to test.")}, "Tests a position the way the game tests its own areas, whether or not the area is enabled and in whatever world.",
                    "True when the position is inside."));
            _class->prototype_function("containsPlayer", &JS_ContainsPlayer,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player | Horse | Cart | Npc", false, "The body to test; a player who has not reported a pose yet is never inside.")},
                    "Tests where a body stands right now, and in which world: an area bound to another virtual world does not contain it. This is containment only -- whether the game considers the player to be trespassing is decided by its own crime system on the player's client.",
                    "True when the body is inside."));
            _class->prototype_function("setShape", &JS_SetShape,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("definition", "Partial<AreaDefinition>", false, "The shape fields to change; any left out keep their value. `id`, `labels`, `metadata` and the rest are ignored here.")},
                    "Moves, turns or reshapes a script area. Bodies standing where it now is, or was, get their `areaEnter` and `areaExit` on the next tick. A level area cannot be reshaped.", "False for a level area, a gone one, or a shape the engine could not build."));
            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("boolean", {}, "Removes a script area. Every body still inside gets its `areaExit` first, while the handle still resolves.", "False for a level area or one already gone."));
            _class->prototype_function("toJSON", &JS_ToJSON,
                v8pp::metadata::docs("AreaDefinition | null", {}, "The definition `Area.create` takes to build this area again, which is what a script saves: the server keeps nothing past a restart. It is the format the World Builder exports too.",
                    "Null for a level area, which the level always builds."));

            const auto herbWorld =
                v8pp::metadata::param("virtualWorld", "number", true, "One virtual-world bucket; defaults to the area's world, or 0 for an area in every world. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.");
            _class->prototype_function("getHerbs", &JS_GetHerbs,
                v8pp::metadata::docs("GatheringHerb[]", {herbWorld},
                    "Lists available authored herbs whose source positions are inside this area's current geometry. Works without nearby players. Picked and script-removed plants are excluded. Ignores area.enabled; an area in another world or a destroyed area returns an empty array."));
            _class->prototype_function("removeHerbs", &JS_RemoveHerbs,
                v8pp::metadata::docs("number", {herbWorld},
                    "Removes authored herbs inside this area's current geometry for one world, including plants already waiting to regrow. Gives no items or experience and raises no harvest events. Plants stay removed until an explicit respawn or server restart, even if the area's geometry, enabled state or lifetime changes.",
                    "Number of sources newly marked for removal; repeated removal returns 0. Includes picked plants whose regrowth was disabled."));
            _class->prototype_function("respawnHerbs", &JS_RespawnHerbs,
                v8pp::metadata::docs("number", {herbWorld},
                    "Restores picked and script-removed herbs inside this area's current geometry in one world. Restores vegetation for connected players and future arrivals. Ignores area.enabled; an area in another world or a destroyed area changes nothing.",
                    "Number of active harvested or removed sources restored."));
            _class->property("exists", &Area::Exists, v8pp::metadata::property_docs("boolean", "False once a script area has been destroyed; every other read then returns its empty value."));
            _class->property("id", &Area::GetId,
                v8pp::metadata::property_docs("string",
                    "The stable identity to store an area under. A level area's is its EntityGuid as sixteen lowercase hex digits, the same on every machine and every boot, as `door.guid` prints a door's; a script area's is the id it was created with."));
            _class->property("kind", &Area::GetKind, v8pp::metadata::property_docs("'level' | 'script'", "Whether the level placed it or a script built it."));
            _class->property("type", &Area::GetType,
                v8pp::metadata::property_docs("'TriggerArea' | 'AreaUnion' | 'SmartAreaShape' | 'box' | 'shape' | 'sphere'", "A level area's entity class -- a `TriggerArea` prism, an `AreaUnion` of them, or a `SmartAreaShape` -- or a script area's shape."));
            _class->property("name", &Area::GetName, &Area::SetName, v8pp::metadata::property_docs("string", "The level's entity name, or the script area's name. Only a script area's can be changed."));
            _class->property("labels", &Area::GetLabels, &Area::SetLabels,
                v8pp::metadata::property_docs("string[]", "The level's own `Label` values -- `private`, `personal`, `interior`, `prohibited`, `castle` and the rest the game's AI reads -- or a script area's labels. Only a script area's can be changed."));
            _class->property("metadata", &Area::GetMetadata, &Area::SetMetadata,
                v8pp::metadata::property_docs("Record<string, unknown>", "Whatever a script keeps on a script area: ownership, a type, rules. Stored as JSON, so read it, change it and assign it back. Always empty on a level area."));
            _class->property("collision", &Area::HasCollision, &Area::SetCollision, v8pp::metadata::property_docs("boolean", "Invisible boundary walls block both directions while enabled, leaving the interior empty. Authored areas only; invalid collision shapes throw."));
            _class->property("collisionFloor", &Area::HasCollisionFloor, &Area::SetCollisionFloor,
                v8pp::metadata::property_docs("boolean", "Whether a box or polygon boundary also closes its bottom with a plate across the whole interior. False by default, for side walls only. A sphere is always one closed shell. Authored areas only."));
            _class->property("collisionCeiling", &Area::HasCollisionCeiling, &Area::SetCollisionCeiling,
                v8pp::metadata::property_docs("boolean", "Whether a box or polygon boundary also closes its top with a plate across the whole interior. False by default, for side walls only. A sphere is always one closed shell. Authored areas only."));
            _class->property("collisionThickness", &Area::GetCollisionThickness, &Area::SetCollisionThickness,
                v8pp::metadata::property_docs("number", "How thick the boundary is, in metres, centred on the drawn outline: 0.2 by default, from 0.05 to 2. Throws when the shape is too small for it while collision is on. Authored areas only."));
            _class->property("enabled", &Area::IsEnabled, &Area::SetEnabled,
                v8pp::metadata::property_docs("boolean", "Whether crossings are raised and the lookups find it. Switching it off raises `areaExit` for everyone inside on the next tick; switching it on, `areaEnter`. Works on level areas too."));
            _class->property("virtualWorld", &Area::GetVirtualWorld, &Area::SetVirtualWorld,
                v8pp::metadata::property_docs("number | null", "The one virtual world a script area belongs to, or null for all of them. A body in another world still crosses it, with `matchingVirtualWorld` false. A level area is in every world."));
            _class->property("position", &Area::GetPosition, &Area::SetPosition, v8pp::metadata::property_docs("Vector3", "A script area's origin, which its shape is placed from; assignment moves it. A level area's is the centre of its bounds."));
            _class->property("rotation", &Area::GetRotation, &Area::SetRotation, v8pp::metadata::property_docs("Quaternion", "A script area's orientation; assignment turns it. A level area's shape is already in world space, so its rotation is the identity."));
            _class->property("height", &Area::GetHeight, v8pp::metadata::property_docs("number", "How far above its lowest point a prism reaches, in metres; 0 for a shape with no ceiling. A TriggerArea's comes from the level's baked prism."));
            _class->property("location", &Area::GetLocation, v8pp::metadata::property_docs("string", "For a SmartAreaShape bound to one of the game's map locations, that location's English name, e.g. a settlement. Empty otherwise."));
            _class->property("bounds", &Area::GetBounds, v8pp::metadata::property_docs("{ min: Vector3; max: Vector3 }", "The world box around the area. An area with no ceiling reaches far above and below."));
            _class->property("members", &Area::GetMembers, v8pp::metadata::property_docs("Area[]", "For an AreaUnion, the TriggerAreas it joins; it contains whatever one of them does. Empty otherwise."));
            _class->property("doors", &Area::GetDoors,
                v8pp::metadata::property_docs("AreaDoor[]", "The doors the level itself links to this area as its `crime_door`s, which is how the game knows a house's doors. Only native links: nothing is inferred from where a door stands."));
            _class->property("players", &Area::GetPlayers, v8pp::metadata::property_docs("Player[]", "Every player inside as of the last tick, in any world."));
            _class->property("occupants", &Area::GetOccupants, v8pp::metadata::property_docs("(Player | Horse | Cart | Npc)[]", "Every body inside as of the last tick: players, horses, carts and server NPCs."));
        }
        return *_class;
    }

    void Area::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Area::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &definition = ServerCatalog().data_type("AreaDefinition");
        definition.add_property("id", "string", "The area's identity, unique among script areas. It may not be sixteen hex digits, which is how a level area's GUID reads.");
        definition.add_property("name", v8pp::metadata::type {"string | undefined", {}, true}, "A readable name; the id when omitted.");
        definition.add_property("type", "'box' | 'shape' | 'sphere'", "CryEngine's own area shapes: `AreaBox`, `AreaShape` (a closed polygon with a height) and `AreaSphere`.");
        definition.add_property("position", v8pp::metadata::type {"Vector3 | undefined", {}, true}, "Where the shape is placed from; the origin when omitted.");
        definition.add_property("rotation", v8pp::metadata::type {"Quaternion | Vector3 | undefined", {}, true}, "The shape's orientation: a Quaternion or {x, y, z, w}, or Euler angles in degrees.");
        definition.add_property("min", v8pp::metadata::type {"Vector3 | undefined", {}, true}, "box: the corner nearest negative infinity, relative to `position` before rotation.");
        definition.add_property("max", v8pp::metadata::type {"Vector3 | undefined", {}, true}, "box: the opposite corner. Every component must be above `min`'s.");
        definition.add_property("points", v8pp::metadata::type {"Vector3[] | undefined", {}, true}, "shape: 3 to 256 corners relative to `position` before rotation, closed back to the first. As in the engine, the floor is the lowest corner.");
        definition.add_property("height", v8pp::metadata::type {"number | undefined", {}, true}, "shape: how far above its lowest corner the area reaches; 0, the default, for no ceiling.");
        definition.add_property("radius", v8pp::metadata::type {"number | undefined", {}, true}, "sphere: its radius around `position`.");
        definition.add_property("labels", v8pp::metadata::type {"string[] | undefined", {}, true}, "Free-form labels, found again with `Area.withLabel`.");
        definition.add_property("metadata", v8pp::metadata::type {"Record<string, unknown> | undefined", {}, true}, "Anything JSON can hold, kept on the area for scripts.");
        definition.add_property("virtualWorld", v8pp::metadata::type {"number | undefined", {}, true}, "The one virtual world it belongs to; every world when omitted.");
        definition.add_property("collision", v8pp::metadata::type {"boolean | undefined", {}, true},
            "Invisible boundary collision from both sides; false when omitted. Polygons need a simple outline and positive height. Boxes and polygons get side walls only unless `collisionFloor` or `collisionCeiling` asks for more; spheres use a tessellated shell.");
        definition.add_property("collisionFloor", v8pp::metadata::type {"boolean | undefined", {}, true}, "box and shape: also close the bottom of the boundary with a plate across the whole interior; false when omitted.");
        definition.add_property("collisionCeiling", v8pp::metadata::type {"boolean | undefined", {}, true}, "box and shape: also close the top of the boundary with a plate across the whole interior; false when omitted.");
        definition.add_property("collisionThickness", v8pp::metadata::type {"number | undefined", {}, true}, "The boundary's thickness in metres, centred on the outline: 0.2 when omitted, from 0.05 to 2.");
        definition.add_property("enabled", v8pp::metadata::type {"boolean | undefined", {}, true}, "Whether it starts enabled; true when omitted.");

        auto &door = ServerCatalog().data_type("AreaDoor");
        door.add_property("guid", "string", "The door's level EntityGuid as hex, which `Door.find` takes.");
        door.add_property("name", "string", "The entity name the level gives the door.");
        door.add_property("position", "Vector3", "Where the level places the door.");
        door.add_property("link", "'entrance' | 'basic' | 'storage'", "The `crime_doorKind` the level gives the link: the way in, an inner door, or a storage room's.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("areaEnter", "[area: Area, entity: Player | Horse | Cart | Npc, matchingVirtualWorld: boolean]",
            "Dispatched when a body comes to be inside an enabled area: it walked or rode in, or the area was created, moved, reshaped or enabled around it. The server decides this itself from the pose it already replicates, by the game's own rule for that kind of area, so a client cannot claim it. `matchingVirtualWorld` is false when a script area bound to one world is crossed by a body in another; a level area is in every world.");
        events.add_property("areaExit", "[area: Area, entity: Player | Horse | Cart | Npc, matchingVirtualWorld: boolean]",
            "Dispatched when a body stops being inside an area: it walked out, or the area was moved, reshaped, disabled or destroyed. A body that leaves the server entirely -- a player disconnecting, a horse despawned -- raises nothing here; its own event already says it is gone.");

        v8pp::class_<Area> &cls = GetClass(isolate);
        cls.static_function("create", &JS_Create,
            v8pp::metadata::docs("Area", {v8pp::metadata::param("definition", "AreaDefinition", false, "The area to build, in the format `area.toJSON()` and the World Builder export write.")},
                "Builds a script area. Bodies already standing in it get `areaEnter` on the next tick. Kept in memory only: save `area.toJSON()` wherever the resource keeps its data, and create it again on boot.",
                "The new area. Throws for an id already taken or a shape the engine could not build."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Area | null", {v8pp::metadata::param("id", "string", false, "A script area's id, or a level area's GUID in hex as `area.definition.id` prints it.")}, "Looks an area up by its stable identity.", "The area, or null when there is none."));
        cls.static_function("getAt", &JS_GetAt,
            v8pp::metadata::docs("Area[]",
                {
                    v8pp::metadata::param("position", "Vector3", false, "The world position to test."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional world; when given, a script area bound to another world is left out."),
                },
                "Every enabled area containing a position, level and script alike.", "The areas, in no particular order."));
        cls.static_function("getAtPlayer", &JS_GetAtPlayer,
            v8pp::metadata::docs("Area[]", {v8pp::metadata::param("player", "Player | Horse | Cart | Npc", false, "The body whose position and world to test.")}, "Every enabled area the body stands in right now, in its own world.",
                "The areas; empty for a player who has not reported a pose yet."));
        cls.static_function("nearby", &JS_Nearby,
            v8pp::metadata::docs("Area[]",
                {
                    v8pp::metadata::param("position", "Vector3", false, "Where to look from."),
                    v8pp::metadata::param("radius", "number", false, "How far, in metres, from the area's bounds."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional world; when given, a script area bound to another world is left out."),
                },
                "Every area whose bounds come within a radius of a position, enabled or not.", "The areas, nearest first."));
        cls.static_function("withLabel", &JS_WithLabel,
            v8pp::metadata::docs("Area[]", {v8pp::metadata::param("label", "string", false, "The label, compared without regard to case.")}, "Every area carrying a label, such as `private` or `castle`.", "The areas, level ones first."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("Area[]", {v8pp::metadata::param("kind", "'level' | 'script'", true, "Optional: only the level's areas, or only script ones.")}, "Every area the server knows.", "The areas, level ones first."));
        cls.publish(global);
    }

    namespace Events {
        void AreaCrossed(bool enter, std::uint32_t areaHandle, std::uint64_t networkId, KCDC::Features::Area::AreaOccupantKind kind, bool matchingVirtualWorld) {
            WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
                const v8::Local<v8::Value> occupant = WrapOccupant(isolate, networkId, kind);
                if (occupant->IsNull()) {
                    return;
                }
                std::vector<v8::Local<v8::Value>> args {WrapArea(isolate, areaHandle), occupant, v8::Boolean::New(isolate, matchingVirtualWorld)};
                resources->GetEvents().EmitReserved(isolate, context, enter ? "areaEnter" : "areaExit", args);
            });
        }
    } // namespace Events
} // namespace KCDC::Scripting
