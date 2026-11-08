#include "door_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/area/area_scripting.h"

#include "shared/features/door/door_definition.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/door_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include "shared/scripting/level_guid.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // Unguarded on purpose: PostInit installs the door service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::Door::DoorService &ResolveDoors() {
            return Server::Get()->GetDoors();
        }

        // The handle a prototype function was called on, or nullptr.
        Door *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Door>::unwrap_object(info.GetIsolate(), info.This());
        }

        // The virtual world an argument names, or the global one every body starts in.
        MafiaNet::VirtualWorldId WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return MafiaNet::VIRTUAL_WORLD_GLOBAL;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // The object `Door.queryNearest` resolves to, found or not.
        v8::Local<v8::Value> NearestToObject(v8::Isolate *isolate, v8::Local<v8::Context> context, const KCDC::Features::Door::DoorService::NearestDoor &answer, MafiaNet::VirtualWorldId virtualWorld) {
            v8::Local<v8::Object> result = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "found"), v8pp::to_v8(isolate, answer.found)).Check();
            result->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, answer.reason)).Check();
            if (!answer.found) {
                result->Set(context, v8pp::to_v8(isolate, "door"), v8::Null(isolate)).Check();
                return result;
            }

            // What the client standing at it sees, not what the replica last carried.
            result->Set(context, v8pp::to_v8(isolate, "guid"), v8pp::to_v8(isolate, FormatLevelGuid(answer.doorGuid))).Check();
            result->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, answer.name)).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, answer.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "open"), v8pp::to_v8(isolate, answer.open)).Check();
            result->Set(context, v8pp::to_v8(isolate, "locked"), v8pp::to_v8(isolate, answer.locked)).Check();

            // Resolved before the answer settled, so there is always one by here.
            Shared::Entities::DoorEntity *door = ResolveDoors().Find(answer.doorGuid, virtualWorld);
            result->Set(context, v8pp::to_v8(isolate, "door"), door != nullptr ? WrapDoor(isolate, door->GetNetworkID()) : v8::Null(isolate)).Check();
            return result;
        }

        // JS: Door.all() -> Door[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> doors     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *door : ResolveDoors().Collect(std::nullopt)) {
                doors->Set(context, index++, WrapDoor(isolate, door->GetNetworkID())).Check();
            }

            info.GetReturnValue().Set(doors);
        }

        // JS: Door.getById(id) -> Door | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.getById: expected (id)"));
                return;
            }

            info.GetReturnValue().Set(WrapDoor(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: Door.find(guid, virtualWorld) -> Door | null
        void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.find: expected (guid, virtualWorld?)"));
                return;
            }

            const std::uint64_t guid = ParseLevelGuid(v8pp::from_v8<std::string>(isolate, info[0]));
            if (guid == 0) {
                info.GetReturnValue().SetNull();
                return;
            }

            Shared::Entities::DoorEntity *door = ResolveDoors().Resolve(guid, WorldArg(info, 1));
            info.GetReturnValue().Set(door != nullptr ? WrapDoor(isolate, door->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Door.getForArea(areaId) -> AreaDoor[]
        void JS_GetForArea(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.getForArea: expected (areaId)"));
                return;
            }

            // From the catalog, so a door no client has reported yet is listed too; `Door.find` resolves the live one.
            KCDC::Features::Area::AreaService &areas = Server::Get()->GetAreas();
            const KCDC::Features::Area::AreaRecord *area = areas.FindById(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(AreaDoorsToArray(isolate, area != nullptr ? areas.DoorsOf(*area) : std::vector<KCDC::Features::Area::AreaDoor> {}));
        }

        // JS: Door.queryNearest(player) -> Promise<NearestDoor>
        void JS_QueryNearest(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            auto *handle      = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[0]) : nullptr;
            auto *replication = Framework::CoreModules::GetReplication();
            auto *caller      = handle != nullptr && replication != nullptr ? replication->GetEntity<Shared::Entities::HumanEntity>(handle->GetId()) : nullptr;
            if (caller == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.queryNearest: expected (player)"));
                return;
            }

            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            info.GetReturnValue().Set(resolver->GetPromise());

            auto pending                               = std::make_shared<PendingPromise>(isolate, context, resolver);
            const MafiaNet::VirtualWorldId callerWorld = caller->GetVirtualWorld();

            // The answer lands on the server tick, so the isolate is re-entered rather than held open.
            const bool asked = ResolveDoors().QueryNearest(*caller, [pending, callerWorld](const KCDC::Features::Door::DoorService::NearestDoor &answer) {
                pending->Settle([&](v8::Isolate *iso, v8::Local<v8::Context> ctx) {
                    return NearestToObject(iso, ctx, answer, callerWorld);
                });
            });

            if (!asked) {
                resolver->Resolve(context, NearestToObject(isolate, context, {false, "The server has no peer to ask.", 0, {}, 0.0F, false, false}, callerWorld)).Check();
                pending->Release();
            }
        }

        // JS: Door.spawn(definition, virtualWorld?) -> Door
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            // A Vector3 and a Quaternion stringify to their components, so a script's own values and a map's arrays come down to the one reader the World Builder's files go through.
            const std::optional<std::string> text = info.Length() > 0 && info[0]->IsObject() ? StringifyValue(isolate, info[0]) : std::nullopt;
            if (!text.has_value()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.spawn: expected (definition, virtualWorld?)"));
                return;
            }
            KCDC::Shared::Game::DoorDefinition definition;
            std::string error;
            if (!KCDC::Shared::Game::DoorDefinitionFromJson(nlohmann::json::parse(*text, nullptr, false), definition, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.spawn: " + error));
                return;
            }
            Shared::Entities::DoorEntity *door = ResolveDoors().Spawn(definition, WorldArg(info, 1), error);
            if (door == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.spawn: " + error));
                return;
            }
            info.GetReturnValue().Set(WrapDoor(isolate, door->GetNetworkID()));
        }

        // JS: Door.models() -> DoorModelInfo[]
        void JS_Models(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> models    = v8::Array::New(isolate, static_cast<int>(KCDC::Shared::Game::g_DoorModels.size()));
            std::uint32_t index            = 0;
            for (const KCDC::Shared::Game::DoorModel &model : KCDC::Shared::Game::g_DoorModels) {
                std::string_view stem(model.path);
                stem                      = stem.substr(stem.find_last_of('/') + 1);
                stem                      = stem.substr(0, stem.find_last_of('.'));
                v8::Local<v8::Object> row = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "path"), v8pp::to_v8(isolate, model.path)).Check();
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(stem))).Check();
                row->Set(context, v8pp::to_v8(isolate, "hinge"), v8pp::to_v8(isolate, std::string_view(model.animSet).ends_with("Right") ? "right" : "left")).Check();
                row->Set(context, v8pp::to_v8(isolate, "placements"), v8pp::to_v8(isolate, static_cast<std::uint32_t>(model.placements))).Check();
                models->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(models);
        }

        // The level door a GUID argument names, or 0 after throwing.
        std::uint64_t GuidArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage) {
            v8::Isolate *isolate     = info.GetIsolate();
            const std::uint64_t guid = info.Length() > 0 && info[0]->IsString() ? ParseLevelGuid(v8pp::from_v8<std::string>(isolate, info[0])) : 0;
            if (guid == 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(usage) + ": expected (guid, virtualWorld?)"));
            }
            return guid;
        }

        // JS: Door.remove(guid, virtualWorld?) -> void
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            const std::uint64_t guid = GuidArg(info, "Door.remove");
            if (guid == 0) {
                return;
            }
            std::string error;
            if (!ResolveDoors().Remove(guid, WorldArg(info, 1), error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Door.remove: " + error));
            }
        }

        // JS: Door.restore(guid, virtualWorld?) -> Door | null
        void JS_Restore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            const std::uint64_t guid = GuidArg(info, "Door.restore");
            if (guid == 0) {
                return;
            }
            const MafiaNet::VirtualWorldId virtualWorld = WorldArg(info, 1);
            if (!ResolveDoors().Unremove(guid, virtualWorld)) {
                info.GetReturnValue().SetNull();
                return;
            }
            Shared::Entities::DoorEntity *door = ResolveDoors().Resolve(guid, virtualWorld);
            info.GetReturnValue().Set(door != nullptr ? WrapDoor(isolate, door->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Door.removed(virtualWorld?) -> RemovedDoor[]
        void JS_Removed(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context                             = isolate->GetCurrentContext();
            const std::optional<MafiaNet::VirtualWorldId> virtualWorld = info.Length() > 0 && info[0]->IsNumber() ? std::optional<MafiaNet::VirtualWorldId>(WorldArg(info, 0)) : std::nullopt;
            v8::Local<v8::Array> rows                                  = v8::Array::New(isolate);
            std::uint32_t index                                        = 0;
            for (const auto &removed : ResolveDoors().CollectRemoved(virtualWorld)) {
                v8::Local<v8::Object> row = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "guid"), v8pp::to_v8(isolate, FormatLevelGuid(removed.doorGuid))).Check();
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, removed.placement->name)).Check();
                row->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3(removed.placement->positionX, removed.placement->positionY, removed.placement->positionZ))).Check();
                row->Set(context, v8pp::to_v8(isolate, "virtualWorld"), v8pp::to_v8(isolate, static_cast<double>(removed.virtualWorld))).Check();
                rows->Set(context, index++, row).Check();
            }
            info.GetReturnValue().Set(rows);
        }

        // JS: door.destroy() -> void
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            Door *self                         = Self(info);
            Shared::Entities::DoorEntity *door = self != nullptr ? self->ResolveDoor() : nullptr;
            if (door == nullptr) {
                return;
            }
            if (door->IsSpawned()) {
                ResolveDoors().Despawn(*door);
                return;
            }
            std::string error;
            if (!ResolveDoors().Remove(door->doorGuid, door->GetVirtualWorld(), error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "door.destroy: " + error));
            }
        }

        // JS: door.toJSON() -> DoorDefinition | null
        void JS_ToJSON(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            Door *self                                           = Self(info);
            Shared::Entities::DoorEntity *door                   = self != nullptr ? self->ResolveDoor() : nullptr;
            const KCDC::Shared::Game::DoorDefinition *definition = door != nullptr ? ResolveDoors().DefinitionOf(*door) : nullptr;
            if (definition == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            // What it was hung as, and whether it is locked now: what a script saves to hang it again.
            KCDC::Shared::Game::DoorDefinition saved = *definition;
            saved.locked                             = door->locked;
            info.GetReturnValue().Set(ParseJsonValue(isolate, KCDC::Shared::Game::DoorDefinitionToJson(saved).dump(), v8::Null(isolate)));
        }

        // JS: door.setOpen(open, fromFront?) -> boolean
        void JS_SetOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Door *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsBoolean() || (info.Length() >= 2 && !info[1]->IsBoolean() && !info[1]->IsUndefined())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "door.setOpen: expected (open, fromFront?)"));
                return;
            }

            Shared::Entities::DoorEntity *door = self->ResolveDoor();
            if (door == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            const bool open      = info[0]->IsTrue();
            const bool fromFront = info.Length() >= 2 && info[1]->IsBoolean() ? info[1]->IsTrue() : door->openedFromFront;
            info.GetReturnValue().Set(ResolveDoors().SetOpen(*door, open, fromFront));
        }
    } // namespace

    bool Events::DoorInteract(std::uint64_t playerNetworkId, std::uint64_t doorNetworkId, const char *action, bool keySide) {
        return EmitReservedEventSync("doorInteract", [playerNetworkId, doorNetworkId, action, keySide](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapDoor(isolate, doorNetworkId));
            args.push_back(v8pp::to_v8(isolate, action));
            args.push_back(v8pp::to_v8(isolate, keySide));
            return args;
        });
    }

    Shared::Entities::DoorEntity *Door::ResolveDoor() const {
        return dynamic_cast<Shared::Entities::DoorEntity *>(Resolve());
    }

    std::string Door::GetGuid() const {
        auto *door = ResolveDoor();
        return FormatLevelGuid(door != nullptr ? door->doorGuid : 0);
    }

    std::uint32_t Door::GetStateToken() const {
        auto *door = ResolveDoor();
        return door != nullptr ? door->stateToken : 0;
    }

    bool Door::IsOpen() const {
        auto *door = ResolveDoor();
        return door != nullptr && door->open;
    }

    bool Door::IsOpenedFromFront() const {
        auto *door = ResolveDoor();
        return door != nullptr && door->openedFromFront;
    }

    bool Door::IsLocked() const {
        auto *door = ResolveDoor();
        return door != nullptr && door->locked;
    }

    void Door::SetLocked(bool locked) {
        if (auto *door = ResolveDoor()) {
            ResolveDoors().SetLock(*door, locked);
        }
    }

    bool Door::IsLockedByServer() const {
        auto *door = ResolveDoor();
        return door != nullptr && ResolveDoors().IsLockedByServer(*door);
    }

    bool Door::IsUsable() const {
        auto *door = ResolveDoor();
        return door != nullptr && door->usable;
    }

    void Door::SetUsable(bool usable) {
        if (auto *door = ResolveDoor()) {
            ResolveDoors().SetUsable(*door, usable);
        }
    }

    std::string Door::GetName() const {
        auto *door                                         = ResolveDoor();
        const KCDC::Shared::Game::DoorPlacement *placement = door != nullptr ? ResolveDoors().PlacementOf(*door) : nullptr;
        return placement != nullptr ? placement->name : std::string {};
    }

    std::string Door::GetKeyItem() const {
        auto *door                                         = ResolveDoor();
        const KCDC::Shared::Game::DoorPlacement *placement = door != nullptr ? ResolveDoors().PlacementOf(*door) : nullptr;
        return placement != nullptr ? placement->keyItemClass : std::string {};
    }

    bool Door::IsLockpickable() const {
        auto *door                                         = ResolveDoor();
        const KCDC::Shared::Game::DoorPlacement *placement = door != nullptr ? ResolveDoors().PlacementOf(*door) : nullptr;
        return placement != nullptr && placement->lockpickable;
    }

    bool Door::StartsLocked() const {
        auto *door                                         = ResolveDoor();
        const KCDC::Shared::Game::DoorPlacement *placement = door != nullptr ? ResolveDoors().PlacementOf(*door) : nullptr;
        return placement != nullptr && placement->startsLocked;
    }

    bool Door::IsSpawned() const {
        auto *door = ResolveDoor();
        return door != nullptr && door->IsSpawned();
    }

    std::string Door::GetModel() const {
        auto *door                                         = ResolveDoor();
        const KCDC::Shared::Game::DoorPlacement *placement = door != nullptr ? ResolveDoors().PlacementOf(*door) : nullptr;
        const KCDC::Shared::Game::DoorModel *model         = placement != nullptr ? KCDC::Shared::Game::DoorModelAt(placement->model) : nullptr;
        return model != nullptr ? model->path : std::string {};
    }

    v8::Local<v8::Value> Door::GetAreas(v8::Isolate *isolate) const {
        auto *door = ResolveDoor();
        return AreasToArray(isolate, door != nullptr ? Server::Get()->GetAreas().AreasForDoor(door->doorGuid) : std::vector<KCDC::Features::Area::AreaRecord *> {});
    }

    std::string Door::ToString() const {
        std::ostringstream ss;
        ss << "Door{ id: " << GetId() << ", guid: " << GetGuid() << ", open: " << (IsOpen() ? "true" : "false") << ", locked: " << (IsLocked() ? "true" : "false") << " }";
        return ss.str();
    }

    v8pp::class_<Door> &Door::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Door>>(isolate, ServerCatalog(), "Door", "Replicated handle for a door: one of the level's own, or one a script or a world resource hung with `Door.spawn`.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(
                    v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for a door the server already knows; use `Door.spawn` to hang a new one, or `Door.find` for one the level places."))
                .function("toString", &Door::ToString, v8pp::metadata::docs("string", {}, "Formats this door handle for logging and debugging.", "The door ID, its level GUID and whether it is open and locked."));

            _class->prototype_function("setOpen", &JS_SetOpen,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("open", "boolean", false, "True to open the door, false to close it."),
                        v8pp::metadata::param("fromFront", "boolean", true, "Which side the leaf is worked from, and so which way it swings. Omitted keeps the side it was last worked from."),
                    },
                    "Opens or closes the door with nobody pushing it. Every client that has the door built plays the game's own swing; one arriving later finds it posed the same way. Opening a locked door unlocks it, as the game's own `Open` does.",
                    "False when the door is already that way."));
            _class->prototype_function("destroy", &JS_Destroy,
                v8pp::metadata::docs("void", {},
                    "Takes the door out. A door a script or a world resource hung is taken down for good. A door the level places is taken out of this handle's virtual world, collision and all, through a level edit that every client seeing that world applies -- a player in the global world sees every world, so it is hidden for them too; the copy in the global world takes it out of every world. `Door.restore` puts a level door back, and `Door.removed` lists the ones that are out. Kept in memory only, like every level edit: a removal is undone when the server restarts or the resource that made it stops."));
            _class->prototype_function("toJSON", &JS_ToJSON,
                v8pp::metadata::docs("DoorDefinition | null", {}, "The definition `Door.spawn` takes to hang this door again, with whether it is locked now: what a script saves. The server keeps nothing past a restart. It is one entry of a World Builder map's `doors` array.",
                    "Null for a door the level places."));

            _class->property("guid", &Door::GetGuid,
                v8pp::metadata::property_docs("string",
                    "The door's EntityGuid, as sixteen lowercase hex digits, the same on every machine; `Door.find` takes it back. A level door's never changes, so it is the identity to store one under. A hung door's lasts only until the server stops: store its `toJSON()` instead."));
            _class->property("stateToken", &Door::GetStateToken, v8pp::metadata::property_docs("number", "Bumped on every durable change. Clients act on the edges rather than the value, so it only tells one change from the next."));
            _class->property("open", &Door::IsOpen, v8pp::metadata::property_docs("boolean", "Whether the door is standing open. Change it with `setOpen`, which also says which way it swings."));
            _class->property("openedFromFront", &Door::IsOpenedFromFront,
                v8pp::metadata::property_docs("boolean", "Which side the leaf was last worked from, which decides the way it swings. A player arriving later poses the door from this, so everyone sees it swung the same way."));
            _class->property("locked", &Door::IsLocked, &Door::SetLocked,
                v8pp::metadata::property_docs("boolean",
                    "Whether the door is locked. Assignment reaches every client that has the door streamed in, including one already standing at it. A door locked from here can only be opened again from here or with a lockpick: a player unlocking it by hand or with a key is refused."));
            _class->property("lockedByServer", &Door::IsLockedByServer, v8pp::metadata::property_docs("boolean", "Whether the lock was set by a script or an operator rather than by the level. It clears when the door is unlocked, by any route the server accepted."));
            _class->property("usable", &Door::IsUsable, &Door::SetUsable,
                v8pp::metadata::property_docs("boolean",
                    "Whether players may work the door. False hides its prompt to open, close, unlock or pick it, and a player's use is refused on their own machine before the door or the body moves -- no round trip, no swing back. A report against it is refused here too, for a client that did not. "
                    "Assignment reaches every client that has the door streamed in, including one already standing at it. The door stays as it is: `setOpen` and `locked` still work, and NPCs still pass it. Per door, not per player: refuse one player with a client `doorInteract` handler."));
            _class->property("name", &Door::GetName, v8pp::metadata::property_docs("string", "The entity name the level gives the door; for a hung door, the `name` it was hung with, or `spawned_door_<n>` without one."));
            _class->property("keyItem", &Door::GetKeyItem,
                v8pp::metadata::property_docs("string",
                    "The item class GUID of the key the level assigns to the door's keyhole, as `player.giveItem` takes it. Empty when it takes none of its own -- most doors open to the generated home or shop key instead. The server does not see inventories, so this is for a resource that keeps its own."));
            _class->property("lockpickable", &Door::IsLockpickable, v8pp::metadata::property_docs("boolean", "Whether the level lets this door be lockpicked. A lockpick reported on one that cannot be is refused."));
            _class->property("areas", &Door::GetAreas,
                v8pp::metadata::property_docs("Area[]", "The level areas that link this door as one of their `crime_door`s -- the house, workshop or storage room it belongs to, as the level itself records it. Nothing is inferred from where the door stands."));
            _class->property("startsLocked", &Door::StartsLocked, v8pp::metadata::property_docs("boolean", "Whether the level builds this door locked, which is the state it has at boot; for a hung door, whether it was hung locked."));
            _class->property("spawned", &Door::IsSpawned, v8pp::metadata::property_docs("boolean", "Whether a script or a world resource hung this door with `Door.spawn`, rather than the level placing it."));
            _class->property("model", &Door::GetModel, v8pp::metadata::property_docs("string", "The leaf mesh the door hangs, as `Door.models()` lists it: `objects/characters/assets/doors/...cdf`."));
        }
        return *_class;
    }

    void Door::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Door::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &nearest = ServerCatalog().data_type("NearestDoor");
        nearest.add_property("found", "boolean", "Whether the client had a door in sight and it belongs to this level.");
        nearest.add_property("reason", "string", "Why there is no door: nothing in sight, another level, no answer, or a caller who left. Empty when one was found.");
        nearest.add_property("door", "Door | null", "The door handle. Null whenever `found` is false.");
        nearest.add_property("guid", "string | undefined", "The door's level EntityGuid as hex, present only when one was found.");
        nearest.add_property("name", "string | undefined", "The name the level gives the door, often empty. Present only when one was found.");
        nearest.add_property("distance", "number | undefined", "How far the player is from it, in metres. Present only when one was found.");
        nearest.add_property("open", "boolean | undefined", "Whether it is standing open, as the client at it sees right now. Present only when one was found.");
        nearest.add_property("locked", "boolean | undefined", "Whether it is locked, as the client at it sees right now. Present only when one was found.");

        auto &definition = ServerCatalog().data_type("DoorDefinition", "A door to hang where the level has none, as `Door.spawn` takes it and a World Builder map's `doors` array stores it.");
        definition.add_property("model", "string", "The leaf mesh, by path or file stem, from `Door.models()`: `lvl1_door_a_left` or `objects/characters/assets/doors/lvl1_door_a_left.cdf`. Which way it is hinged is part of the mesh.");
        definition.add_property("position", "Vector3 | number[]", "Where the door hangs: the leaf's pivot, on its hinge side, as the level's own doors stand. Each coordinate within 16384 of the origin.");
        definition.add_property("rotation", v8pp::metadata::type {"Quaternion | number[] | undefined", {}, true}, "Which way it faces: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.");
        definition.add_property("name", v8pp::metadata::type {"string | undefined", {}, true}, "What a script finds it by, up to 64 bytes of UTF-8; `door.name` reads it.");
        definition.add_property("locked", v8pp::metadata::type {"boolean | undefined", {}, true}, "Whether it is hung locked. Unlocked when omitted. A key or a lockpick opens it as the level's own doors open; the server's lock (`door.locked = true`) is a separate thing.");
        definition.add_property("lock", v8pp::metadata::type {"'none' | 'bolt' | 'boltAndKey' | 'key' | undefined", {}, true},
            "What each side takes to get through it while it is locked: `none` (only a script opens it), `bolt` (a bolt inside, nothing outside), `boltAndKey` (a bolt inside, a keyhole outside, as most of the level's doors) or `key` (a keyhole on both sides). `boltAndKey` when omitted.");
        definition.add_property("lockpickable", v8pp::metadata::type {"boolean | undefined", {}, true}, "Whether its keyhole can be picked. True when omitted.");
        definition.add_property("lockDifficulty", v8pp::metadata::type {"number | undefined", {}, true}, "How hard the lockpicking minigame is, from 0 to 1, read in fifths from very easy to very hard. The game's own when omitted.");
        definition.add_property("keyItem", v8pp::metadata::type {"string | undefined", {}, true}, "The item class GUID of a key that works the keyhole, as `player.giveItem` takes it. None when omitted.");

        auto &modelInfo = ServerCatalog().data_type("DoorModelInfo", "A leaf mesh the levels hang doors from.");
        modelInfo.add_property("path", "string", "The mesh path `Door.spawn` takes, `objects/characters/assets/doors/...cdf`.");
        modelInfo.add_property("name", "string", "The file stem, which `Door.spawn` also takes.");
        modelInfo.add_property("hinge", "'left' | 'right'", "Which side the leaf is hinged on.");
        modelInfo.add_property("placements", "number", "How many doors the shipped levels hang from it.");

        auto &removed = ServerCatalog().data_type("RemovedDoor", "A door the level places that a level edit has taken out of a virtual world.");
        removed.add_property("guid", "string", "Its level EntityGuid as sixteen lowercase hex digits, as `Door.restore` takes it.");
        removed.add_property("name", "string", "The entity name the level gives it.");
        removed.add_property("position", "Vector3", "Where the level hangs it.");
        removed.add_property("virtualWorld", "number", "The virtual world it is out of. Out of the global world, it is out of every world.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("doorInteract", "[player: Player, door: Door, action: \"open\" | \"close\" | \"unlock\" | \"lockpick\", keySide: boolean]",
            "Dispatched when a player's client reports working a door, before the server applies it. `action` is `open`, `close`, `unlock` or `lockpick`; a key turned in the same use as the push arrives as `unlock` and then `open`, and refusing the `unlock` refuses the report without asking about the `open`. `keySide` says whether the player stood on the side with the keyhole, which is where an unlock needs a key.\n\n"
            "Return false to refuse it: the door is put back on every client, the player's included, and nobody else sees it happen. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. The server has already refused what the game's own rules forbid -- a player out of reach, a door the server locked, opening a locked door, picking a door with no keyhole, a door a script made unusable -- so this only sees what the game would allow.\n\n"
            "The player's own client moves the door as soon as they use it and reports afterwards, so a refusal here reaches them a round trip later, as the door swinging back. It is the authority, for what only the server knows and for a client that ignored its own rules. To refuse without that, set `door.usable`, or refuse in the client's own `doorInteract`: both stop the use on the player's machine before anything moves.");

        v8pp::class_<Door> &cls = GetClass(isolate);

        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Door[]", {},
                "Lists every door: the level's own, all of which exist from boot in the global world, and every door a script or a world resource hung. In another virtual world a level door is built the first time anything there reaches it, so this lists only those. A level door taken out of a world is not listed for it; `Door.removed` lists those.",
                "One handle per known door, in no particular order."));
        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Door",
                {
                    v8pp::metadata::param("definition", "DoorDefinition", false, "The leaf, where it hangs and how it locks, in the format of one entry of a World Builder map's `doors` array."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world whose players see it; the global one when omitted."),
                },
                "Hangs a working door where the level has none: every player in the virtual world sees it, opens and closes it, keys or picks its lock, and the server judges every use by the same rules as the level's own doors. It starts closed. Kept in memory only: hang it again on boot. The resource that hangs it owns it, so it goes when that resource stops. NPCs do not path through it: the level's navigation mesh has no link for it.",
                "The door. Throws for an unknown model, a transform that is not finite, a lock name that is not one of the four, or a key that is not an item class."));
        cls.static_function("models", &JS_Models, v8pp::metadata::docs("DoorModelInfo[]", {}, "Lists every leaf mesh the shipped levels hang doors from, which is every model `Door.spawn` takes.", "One row per mesh, sorted by path."));
        cls.static_function("remove", &JS_Remove,
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("guid", "string", false, "The level door's EntityGuid as hex, as `door.guid` prints it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to take it out of; the global one, and so every world, when omitted."),
                },
                "Takes a door the level places out of a virtual world, collision and all, as `door.destroy()` does, without needing its handle. Taking out a door that is already out of that world, or of the global one, does nothing; taking out one that has a level edit in that world, a move or a swap, replaces that edit.",
                "Throws for a GUID that is not one of the level's doors."));
        cls.static_function("restore", &JS_Restore,
            v8pp::metadata::docs("Door | null",
                {
                    v8pp::metadata::param("guid", "string", false, "The level door's EntityGuid as hex, as `Door.removed` lists it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to put it back in; the global one when omitted. A door taken out of the global world is only put back from there."),
                },
                "Puts back a level door that a script, a world resource or any other level edit took out of a virtual world. It comes back closed, and locked as the level builds it.", "The door, or null when that world had no such removal."));
        cls.static_function("removed", &JS_Removed,
            v8pp::metadata::docs("RemovedDoor[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")},
                "Lists the doors the level places that are taken out of a virtual world, by a script or a World Builder export alike.", "One row per removal, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Door | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a door up by its network entity ID.", "The door's handle, or null when no live door has that ID."));
        cls.static_function("find", &JS_Find,
            v8pp::metadata::docs("Door | null",
                {
                    v8pp::metadata::param("guid", "string", false, "The door's level EntityGuid as hex, with or without an `0x` prefix, as `door.guid` prints it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one, where every body starts."),
                },
                "Looks a door up by the level's own identity for it, which survives a restart and is the same on every machine. A door the level places is found in any virtual world it has not been taken out of. A door a script hung is found by `door.guid` in the world it hangs in.",
                "The door's handle, or null when there is no such door there."));
        cls.static_function("getForArea", &JS_GetForArea,
            v8pp::metadata::docs("AreaDoor[]", {v8pp::metadata::param("areaId", "string", false, "The area's id, as `area.id` prints it.")},
                "Lists the doors the level links to an area as its `crime_door`s, with their GUID, name, position and link type, straight from the level catalog -- so a door that no client has reported yet is listed too. Pass a `guid` to `Door.find` for the live handle and its `locked` state. Only native links: a script area has none.",
                "The doors; empty for an unknown area or one with no links."));
        cls.static_function("queryNearest", &JS_QueryNearest,
            v8pp::metadata::docs("Promise<NearestDoor>", {v8pp::metadata::param("player", "Player", false, "The player to ask. The question goes to their own client.")},
                "Asks a player's client which door their body is standing at. The server knows every door the level places but has no world to measure distances in, so this is how a command finds the door in front of a player, and the answer is what that client sees now rather than what the replica last carried.\n\nThe promise always settles: on the answer, on a five-second timeout, or when the player leaves, with `found` false and `reason` saying which.",
                "The answer, once that client has given one."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
