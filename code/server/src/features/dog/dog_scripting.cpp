#include "dog_scripting.h"

#include "shared/scripting/rotation_arg.h"
#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/dog/dog_verbs.h"

#include "shared/features/player/player_entity.h"
#include "shared/scripting/catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape the spawn and destroy events have: one subject, nothing else.
        void EmitDogEvent(const char *eventName, std::uint64_t dogNetworkId) {
            EmitReservedEvent(eventName, [dogNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapDog(isolate, dogNetworkId));
                return args;
            });
        }

        // The handle a prototype function was called on, or nullptr.
        Dog *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Dog>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: dog.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Dog::Dogs::Destroy(self->ResolveDog());
            }
        }

        // JS: dog.giveTo(player)
        void JS_GiveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self   = Self(info);
            auto *player = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[0]) : nullptr;
            if (self == nullptr) {
                return;
            }
            if (player == nullptr && (info.Length() < 1 || !info[0]->IsNull())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Dog.giveTo: expected (player | null)"));
                return;
            }
            if (!KCDC::Features::Dog::Dogs::SetOwner(self->ResolveDog(), player != nullptr ? player->GetId() : 0)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Dog.giveTo: not a connected player"));
            }
        }

        // JS: Dog.spawn(owner, position, rotation, soul, name) -> Dog
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *owner = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[0]) : nullptr;
            if (owner == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Dog.spawn: expected (owner, [position], [rotation], [soul], [name])"));
                return;
            }

            // Spawned on its owner unless the caller says otherwise: a dog belongs beside the player it is given to, and the server has no engine to place it anywhere smarter.
            auto *replication = Framework::CoreModules::GetReplication();
            auto *ownerBody   = replication != nullptr ? replication->GetEntity<Shared::Entities::HumanEntity>(owner->GetId()) : nullptr;

            glm::vec3 position = ownerBody != nullptr ? ownerBody->position : glm::vec3(0.0F);
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            std::string soulGuid;
            std::string name;

            // The glm converters in <scripting/v8pp_types.h> are convert<> specialisations; the generic from_v8<T> would try to unwrap a registered class_<glm::vec3> first and throw, so call them directly.
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }
            if (info.Length() >= 3) {
                rotation = ParseRotationArg(isolate, info[2], rotation);
            }
            if (info.Length() >= 4 && info[3]->IsString()) {
                soulGuid = v8pp::from_v8<std::string>(isolate, info[3]);
            }
            if (info.Length() >= 5 && info[4]->IsString()) {
                name = v8pp::from_v8<std::string>(isolate, info[4]);
            }

            auto *dog = KCDC::Features::Dog::Dogs::Spawn(soulGuid, owner->GetId(), position, rotation, name);
            if (dog == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Dog.spawn: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapDog(isolate, dog->GetNetworkID()));
        }

        // JS: Dog.all() -> Dog[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> dogs      = v8::Array::New(isolate);

            std::uint32_t index = 0;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Shared::Entities::DogEntity>([&](Shared::Entities::DogEntity *dog) {
                    dogs->Set(context, index++, WrapDog(isolate, dog->GetNetworkID())).Check();
                });
            }

            info.GetReturnValue().Set(dogs);
        }

        // JS: Dog.getById(id) -> Dog | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Dog.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapDog(isolate, networkId));
        }
    } // namespace

    Shared::Entities::DogEntity *Dog::ResolveDog() const {
        return dynamic_cast<Shared::Entities::DogEntity *>(Resolve());
    }

    std::string Dog::GetSoul() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->soulGuid : std::string();
    }

    std::string Dog::GetName() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->name : std::string();
    }

    void Dog::SetName(const std::string &name) {
        KCDC::Features::Dog::Dogs::Rename(ResolveDog(), name);
    }

    std::uint64_t Dog::GetOwnerId() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->ownerNetworkId : 0;
    }

    v8::Local<v8::Value> Dog::GetOwnerHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        return handleScope.Escape(WrapPlayer(isolate, GetOwnerId()));
    }

    bool Dog::GetDestroyWithOwner() const {
        auto *dog = ResolveDog();
        return dog == nullptr || dog->destroyWithOwner;
    }

    void Dog::SetDestroyWithOwner(bool destroy) {
        if (auto *dog = ResolveDog()) {
            dog->destroyWithOwner = destroy;
        }
    }

    std::uint32_t Dog::GetMode() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->mode : 0U;
    }

    void Dog::SetMode(std::uint32_t mode) {
        // Silently clamped rather than thrown: E_DogCompanionMode has eight values, and a script that asks for a ninth is asking for something the engine would refuse anyway.
        if (mode > 7U) {
            return;
        }
        KCDC::Features::Dog::Dogs::SetMode(ResolveDog(), static_cast<std::uint8_t>(mode));
    }

    std::uint32_t Dog::GetObjective() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->objective : Shared::Entities::DogEntity::kNoObjective;
    }

    float Dog::GetMorale() const {
        auto *dog = ResolveDog();
        return dog != nullptr ? dog->morale : 0.0F;
    }

    std::string Dog::ToString() const {
        std::ostringstream ss;
        ss << "Dog{ id: " << GetId() << ", name: '" << GetName() << "', soul: '" << GetSoul() << "', owner: " << GetOwnerId() << ", mode: " << GetMode() << " }";
        return ss.str();
    }

    v8pp::class_<Dog> &Dog::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Dog>>(isolate, ServerCatalog(), "Dog", "Replicated KCD2 dog companion handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing dog with this ID; use Dog.spawn() to spawn one."))
                .function("toString", &Dog::ToString, v8pp::metadata::docs("string", {}, "Formats this dog handle for logging and debugging.", "The dog ID, its soul, its owner and its mode."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Despawns this dog on every client after emitting dogDestroy."));
            _class->prototype_function("giveTo", &JS_GiveTo,
                v8pp::metadata::docs("void", {v8pp::metadata::param("player", "Player | null", false, "The dog's new master, or null to leave it masterless.")},
                    "Hands this dog to another player. Every client re-possesses the body onto the new master's soul, and authority over its pose moves with it. Throws when `player` is not a connected player."));

            _class->property("soul", &Dog::GetSoul, v8pp::metadata::property_docs("string", "GUID of the soul this dog was spawned against, which decides its appearance."));
            _class->property("name", &Dog::GetName, &Dog::SetName, v8pp::metadata::property_docs("string", "Name every client shows for this dog. Assignment renames it on all of them; empty means the name its soul carries stands."));
            _class->property("ownerId", &Dog::GetOwnerId, v8pp::metadata::property_docs("number", "Network ID of the player this dog belongs to, or 0 when it has no master."));
            _class->property(
                "owner",
                [](const Dog &self, v8::Isolate *isolate) {
                    return self.GetOwnerHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null", "The player this dog belongs to, or null when it has no master."));
            _class->property("destroyWithOwner", &Dog::GetDestroyWithOwner, &Dog::SetDestroyWithOwner,
                v8pp::metadata::property_docs("boolean",
                    "Whether this dog is despawned when its owner disconnects. True by default. Set it to false to keep the dog in the world: it is left masterless instead (`dogOwnerChanged` with a null player), and with nobody's game running its brain it stands where it was until `giveTo` hands it to someone. Saving it across server restarts is the script's own business."));
            _class->property("mode", &Dog::GetMode, &Dog::SetMode, v8pp::metadata::property_docs("number", "The dog's companion mode: 0 Wait, 1 Follow, 2 Free, 3 Aggressive, 4 Search, 5 Hunt, 6 Guard, 7 Ambush. Assignment applies it on every client."));
            _class->property("objective", &Dog::GetObjective,
                v8pp::metadata::property_docs("number",
                    "What the owner's game currently has this dog doing, as an E_DogObjective ordinal: 0 Wait, 2 Bark, 4 Follow, 5 FollowHeel, 7 Search, 8 MeleeCombat, 9 Fetch, 10 Hunt, 19 Eat, 21 Distract, 22 Pet, 25 Invalid when idle. Reported by the owner, so read-only."));
            _class->property("morale", &Dog::GetMorale, v8pp::metadata::property_docs("number", "The dog's morale as its owner last read it off the game, or 0 before any report. Below the game's own threshold the dog stops obeying commands. Owner-reported, so read-only."));
        }
        return *_class;
    }

    void Dog::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Dog::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("dogSpawn", "[dog: Dog]", "Dispatched immediately after a dog is created and replicated, whether by `Dog.spawn` or anything else.");
        events.add_property("dogDestroy", "[dog: Dog]", "Dispatched while a dog is being despawned. The handle still resolves, so its owner and name can be read one last time.");
        events.add_property("dogOwnerChanged", "[dog: Dog, player: Player | null]", "Dispatched after a dog is handed to another player, or left masterless.");
        events.add_property("dogModeChanged", "[dog: Dog, mode: number]", "Dispatched after a dog's companion mode actually changes.");

        v8pp::class_<Dog> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Dog",
                {
                    v8pp::metadata::param("owner", "Player", false, "The player the dog belongs to. A dog is somebody's from the moment it exists."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space spawn position; omitted spawns the dog on its owner."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("soul", "string", true, "Optional soul GUID from the game's tables; omitted spawns the generic dog."),
                    v8pp::metadata::param("name", "string", true, "Optional name every client shows for the dog; omitted leaves the name its soul carries."),
                },
                "Spawns and replicates a dog companion for a player.", "The newly spawned dog handle."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("Dog[]", {}, "Lists every dog the server currently has.", "One handle per live dog, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Dog | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a dog up by its network entity ID.", "The dog's handle, or null when no live dog has that ID."));

        cls.publish(global);
    }

    void Events::DogSpawned(std::uint64_t dogNetworkId) {
        EmitDogEvent("dogSpawn", dogNetworkId);
    }

    void Events::DogDestroyed(std::uint64_t dogNetworkId) {
        EmitDogEvent("dogDestroy", dogNetworkId);
    }

    void Events::DogOwnerChanged(std::uint64_t dogNetworkId, std::uint64_t playerNetworkId) {
        EmitReservedEvent("dogOwnerChanged", [dogNetworkId, playerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapDog(isolate, dogNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        });
    }

    void Events::DogModeChanged(std::uint64_t dogNetworkId, std::uint8_t mode) {
        EmitReservedEvent("dogModeChanged", [dogNetworkId, mode](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapDog(isolate, dogNetworkId));
            args.push_back(v8pp::to_v8(isolate, static_cast<std::uint32_t>(mode)));
            return args;
        });
    }
} // namespace KCDC::Scripting
