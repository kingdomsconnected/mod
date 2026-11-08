#include "prop_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "shared/scripting/rotation_arg.h"

#include "features/prop/prop_verbs.h"

#include "shared/game/prop_models.h"
#include "shared/scripting/catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape both of this feature's events have: one subject, nothing else.
        void EmitPropEvent(const char *eventName, std::uint64_t propNetworkId) {
            EmitReservedEvent(eventName, [propNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapProp(isolate, propNetworkId));
                return args;
            });
        }

        // The virtual world an argument names, or none for every one of them.
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // The handle a prototype function was called on, or nullptr.
        Prop *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Prop>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: prop.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Prop::Props::Destroy(self->ResolveProp());
            }
        }

        // A Vector3 argument, or nullopt when it is absent or undefined. Throws on anything else.
        bool VectorArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *what, std::optional<glm::vec3> &value) {
            if (info.Length() <= index || info[index]->IsUndefined() || info[index]->IsNull()) {
                return true;
            }
            if (!info[index]->IsObject()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), std::string(what) + ": expected a Vector3"));
                return false;
            }
            value = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
            return true;
        }

        // One motion for this prop's body, answered with whether it went out.
        void SendMotion(const v8::FunctionCallbackInfo<v8::Value> &info, Shared::RPC::PropMotion motion) {
            Prop *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Prop::Props::Move(self->ResolveProp(), motion));
        }

        // JS: prop.applyImpulse(impulse, point?, angularImpulse?) -> boolean
        void JS_ApplyImpulse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::optional<glm::vec3> impulse;
            std::optional<glm::vec3> point;
            std::optional<glm::vec3> angular;
            if (!VectorArg(info, 0, "prop.applyImpulse: impulse", impulse) || !VectorArg(info, 1, "prop.applyImpulse: point", point) || !VectorArg(info, 2, "prop.applyImpulse: angularImpulse", angular)) {
                return;
            }
            if (!impulse) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "prop.applyImpulse: expected (impulse, point?, angularImpulse?)"));
                return;
            }
            Shared::RPC::PropMotion motion;
            motion.kind     = static_cast<std::uint8_t>(Shared::RPC::PropMotionKind::Impulse);
            motion.linear   = *impulse;
            motion.angular  = angular.value_or(glm::vec3(0.0F));
            motion.hasPoint = point.has_value();
            motion.point    = point.value_or(glm::vec3(0.0F));
            SendMotion(info, motion);
        }

        // JS: prop.setVelocity(velocity, angularVelocity?) -> boolean
        void JS_SetVelocity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::optional<glm::vec3> velocity;
            std::optional<glm::vec3> angular;
            if (!VectorArg(info, 0, "prop.setVelocity: velocity", velocity) || !VectorArg(info, 1, "prop.setVelocity: angularVelocity", angular)) {
                return;
            }
            if (!velocity) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "prop.setVelocity: expected (velocity, angularVelocity?)"));
                return;
            }
            Shared::RPC::PropMotion motion;
            motion.kind    = static_cast<std::uint8_t>(Shared::RPC::PropMotionKind::Velocity);
            motion.linear  = *velocity;
            motion.angular = angular.value_or(glm::vec3(0.0F));
            SendMotion(info, motion);
        }

        // JS: prop.wake() / prop.sleep() / prop.resetPhysics() -> boolean
        void JS_Wake(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::RPC::PropMotion motion;
            motion.kind = static_cast<std::uint8_t>(Shared::RPC::PropMotionKind::Wake);
            SendMotion(info, motion);
        }

        void JS_Sleep(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::RPC::PropMotion motion;
            motion.kind = static_cast<std::uint8_t>(Shared::RPC::PropMotionKind::Sleep);
            SendMotion(info, motion);
        }

        void JS_ResetPhysics(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Prop *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Prop::Props::Reset(self->ResolveProp()));
        }

        // JS: Prop.spawn(model, position, rotation, scale, physics, virtualWorld) -> Prop
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Prop.spawn: expected (model, position, rotation?, scale?, physics?, virtualWorld?)"));
                return;
            }

            const std::string model      = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::size_t modelIndex = Shared::Game::FindPropModelIndex(model);
            if (modelIndex == Shared::Game::kNoPropModel && !KCDC::Features::Prop::Props::IsStreamedModel(model)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Prop.spawn: " + model + " is neither a mesh in the prop catalog nor one this server streams"));
                return;
            }

            glm::vec3 position {0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            float scale = 1.0F;
            glm::vec3 axisScale {1.0F};
            Shared::Entities::PropPhysics physics = Shared::Entities::PropPhysics::Static;
            std::uint32_t virtualWorld            = MafiaNet::VIRTUAL_WORLD_GLOBAL;

            // Called directly for the reason ParseRotationArg gives.
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }
            if (info.Length() >= 3) {
                rotation = ParseRotationArg(isolate, info[2], rotation);
            }
            if (info.Length() >= 4 && info[3]->IsNumber()) {
                scale = static_cast<float>(v8pp::from_v8<double>(isolate, info[3]));
            }
            if (info.Length() >= 4 && info[3]->IsObject()) {
                axisScale = v8pp::convert<glm::vec3>::from_v8(isolate, info[3]);
            }
            if (info.Length() >= 5 && info[4]->IsString()) {
                const std::string requested = v8pp::from_v8<std::string>(isolate, info[4]);
                if (!Shared::Entities::ParsePropPhysics(requested, physics)) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Prop.spawn: " + requested + " is not a physics kind; expected rigid, static or none"));
                    return;
                }
            }
            if (info.Length() >= 6 && info[5]->IsNumber()) {
                virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[5]));
            }

            auto *prop = KCDC::Features::Prop::Props::Spawn(modelIndex, modelIndex == Shared::Game::kNoPropModel ? model : std::string(), position, rotation, scale, physics, virtualWorld, axisScale);
            if (prop == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Prop.spawn: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapProp(isolate, prop->GetNetworkID()));
        }

        // JS: Prop.all() -> Prop[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> props     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *prop : KCDC::Features::Prop::Props::Collect(std::nullopt)) {
                props->Set(context, index++, WrapProp(isolate, prop->GetNetworkID())).Check();
            }

            info.GetReturnValue().Set(props);
        }

        // JS: Prop.getById(id) -> Prop | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Prop.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapProp(isolate, networkId));
        }

        // JS: Prop.destroyAll(virtualWorld) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Prop::Props::DestroyAll(WorldArg(info, 0))));
        }

    } // namespace

    Shared::Entities::PropEntity *Prop::ResolveProp() const {
        return dynamic_cast<Shared::Entities::PropEntity *>(Resolve());
    }

    std::string Prop::GetModel() const {
        const std::optional<Shared::Game::PropModelEntry> model = KCDC::Features::Prop::Props::ModelOf(ResolveProp());
        return model ? std::string(model->path) : std::string();
    }

    std::string Prop::GetModelName() const {
        const std::optional<Shared::Game::PropModelEntry> model = KCDC::Features::Prop::Props::ModelOf(ResolveProp());
        return model ? std::string(model->name) : std::string();
    }

    std::string Prop::GetModelGroup() const {
        const std::optional<Shared::Game::PropModelEntry> model = KCDC::Features::Prop::Props::ModelOf(ResolveProp());
        return model ? std::string(model->group) : std::string();
    }

    std::string Prop::GetPhysics() const {
        auto *prop = ResolveProp();
        return prop != nullptr ? Shared::Entities::PropPhysicsName(prop->physics) : std::string();
    }

    void Prop::SetPhysics(const std::string &physics) {
        Shared::Entities::PropPhysics parsed = Shared::Entities::PropPhysics::Static;
        if (Shared::Entities::ParsePropPhysics(physics, parsed)) {
            KCDC::Features::Prop::Props::SetPhysics(ResolveProp(), parsed);
        }
    }

    float Prop::GetScale() const {
        auto *prop = ResolveProp();
        return prop != nullptr ? prop->scale : 0.0F;
    }

    void Prop::SetScale(float scale) {
        KCDC::Features::Prop::Props::SetScale(ResolveProp(), scale);
    }

    float Prop::GetMass() const {
        auto *prop = ResolveProp();
        return prop != nullptr ? prop->mass : 0.0F;
    }

    void Prop::SetMass(float mass) {
        KCDC::Features::Prop::Props::SetMass(ResolveProp(), mass);
    }

    std::string Prop::ToString() const {
        std::ostringstream ss;
        ss << "Prop{ id: " << GetId() << ", model: " << GetModelName() << ", physics: " << GetPhysics() << ", scale: " << GetScale() << " }";
        return ss.str();
    }

    v8pp::class_<Prop> &Prop::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Prop>>(isolate, ServerCatalog(), "Prop", "Replicated static mesh handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing prop with this ID; use Prop.spawn() to spawn one."))
                .function("toString", &Prop::ToString, v8pp::metadata::docs("string", {}, "Formats this prop handle for logging and debugging.", "The prop ID, its model, its physics and its scale."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Despawns this prop on every client after emitting propDestroy."));

            // Shared by every motion: what comes back.
            constexpr const char *kMotionReturns = "True when it was sent to the client simulating the prop; false when the prop is gone or is not `rigid`, a value is not finite or is out of range, or no player is within 60 m to simulate it.";
            _class->prototype_function("applyImpulse", &JS_ApplyImpulse,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("impulse", "Vector3", false, "The push in kg m/s, world space. The body's velocity changes by this divided by its mass, so a heavy prop needs a large number."),
                        v8pp::metadata::param("point", "Vector3", true, "Where the push lands, in world space. Off the centre of mass it also turns the body, which is what topples a tall one. Omitted, it lands on the centre of mass."),
                        v8pp::metadata::param("angularImpulse", "Vector3", true, "A twist on top, in kg m^2/s about each world axis."),
                    },
                    "Pushes a `rigid` prop's body and wakes it. The nearest player's client simulates the body and every other player sees the pose it streams. `propRest` is raised once it stops.", kMotionReturns));
            _class->prototype_function("setVelocity", &JS_SetVelocity,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("velocity", "Vector3", false, "Its new velocity in m/s, world space, up to 200."),
                        v8pp::metadata::param("angularVelocity", "Vector3", true, "Its new spin in radians per second about each world axis, up to 100. Omitted, it stops turning."),
                    },
                    "Sets a `rigid` prop's velocity outright, whatever its mass, and wakes it. `propRest` is raised once it stops.", kMotionReturns));
            _class->prototype_function("wake", &JS_Wake, v8pp::metadata::docs("boolean", {}, "Starts a resting `rigid` prop simulating where it lies, so it falls if nothing holds it up. `propRest` is raised once it stops.", kMotionReturns));
            _class->prototype_function("sleep", &JS_Sleep,
                v8pp::metadata::docs("boolean", {},
                    "Stops a `rigid` prop's body where it is, in mid-air if need be. It wakes again when something hits it or a script moves it. `propRest` then reports where it stopped. To fix a prop in place for good, set `physics` to `static` once `propRest` has been raised.",
                    kMotionReturns));
            _class->prototype_function("resetPhysics", &JS_ResetPhysics,
                v8pp::metadata::docs("boolean", {}, "Stops a `rigid` prop's body and puts it back where it was spawned, for every player. Raises no `propRest`.", "True when it was put back; false when the prop is gone or is not `rigid`."));

            _class->property("model", &Prop::GetModel,
                v8pp::metadata::property_docs("string",
                    "Catalog path of the mesh this prop was built from, e.g. `objects/manmade/barrels/barrel_a.cgf`. Read-only: the collision is built from the mesh when the entity is spawned and never re-read, so a prop is the model it was created with."));
            _class->property("modelName", &Prop::GetModelName, v8pp::metadata::property_docs("string", "File stem of the mesh, e.g. `barrel_a`, which is the short name `Prop.spawn` also accepts."));
            _class->property("modelGroup", &Prop::GetModelGroup, v8pp::metadata::property_docs("string", "Browsing bucket the mesh sits in, e.g. `manmade/structures`; `kcdc/<resource>` for a mesh the server streams."));
            _class->property("physics", &Prop::GetPhysics, &Prop::SetPhysics,
                v8pp::metadata::property_docs("string",
                    "What this prop's collision does: `static` collides but never moves, `rigid` falls and can be pushed, `none` has none at all. Assignment rebuilds the prop on every client. A rigid one is simulated by the nearest player's client within 60 m, and every other player sees the pose it streams; with nobody that near, it stays where it last rested."));
            _class->property("mass", &Prop::GetMass, &Prop::SetMass,
                v8pp::metadata::property_docs("number",
                    "A `rigid` prop's mass in kilograms, given to its body as it is built; 0, the default, takes the mass the mesh's collision gives it. Up to 100,000; a value outside 0 to 100,000 is ignored. Set it for a mesh whose collision carries no mass, such as a tree's: without one, every client builds such a prop as a `static` one, which stands still and cannot be pushed, and logs why. Assignment rebuilds the prop on every client, so set it before pushing it."));
            _class->property("scale", &Prop::GetScale, &Prop::SetScale,
                v8pp::metadata::property_docs("number", "Uniform scale, from 0.01 to 100; a value outside that is clamped into it. Assignment rebuilds the prop on every client, because the collision is sized from the scale at spawn and never re-read."));
        }
        return *_class;
    }

    void Prop::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Prop::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("propSpawn", "[prop: Prop]", "Dispatched immediately after a prop is created and replicated, whether by `Prop.spawn`, the `/prop` command, or anything else.");
        events.add_property("propDestroy", "[prop: Prop]", "Dispatched while a prop is being despawned. The handle still resolves, so its model and its pose can be read one last time.");
        events.add_property("propRest", "[prop: Prop, position: Vector3, rotation: Quaternion]",
            "Dispatched when a `rigid` prop's body stops moving, whatever moved it: a script's `applyImpulse`, `setVelocity`, `wake` or `sleep`, a fall, or a player walking into it. The client simulating it says where, and every player already sees it there. This is that client's word, checked only to lie within 100 m of where the prop last rested or was spawned; a prop reported farther is put back there and raises nothing.");

        v8pp::class_<Prop> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Prop",
                {
                    v8pp::metadata::param("model", "string", false,
                        "Mesh to build, as either a full catalog path (`objects/manmade/barrels/barrel_a.cgf`) or its file stem (`barrel_a`). A stem several meshes share resolves to the first of them, so pass the path when it matters which. A mesh this server streams -- a `.cgf` in a resource's `stream/objects/kcdc/<resource>/` folder -- is named by its full path, `objects/kcdc/<resource>/chair.cgf`; a player whose game does not have it yet sees the prop as soon as it does."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space spawn position; omitted components default to zero."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("scale", "number | Vector3", true, "Uniform size or separate X/Y/Z sizes, each from 0.01 to 100. Omitted uses the mesh size. Nonuniform collision follows the native engine limitations."),
                    v8pp::metadata::param("physics", "string", true, "Optional collision: `static` (the default), `rigid` or `none`."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the prop belongs to; omitted puts it in the global one."),
                },
                "Spawns and replicates a static mesh from the game's own object catalog.", "The newly spawned prop handle."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("Prop[]", {}, "Lists every prop the server currently has.", "One handle per live prop, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Prop | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a prop up by its network entity ID.", "The prop's handle, or null when no live prop has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Despawns props, emitting propDestroy for each one.", "How many props were removed."));
        cls.publish(global);
    }

    void Events::PropSpawned(std::uint64_t propNetworkId) {
        EmitPropEvent("propSpawn", propNetworkId);
    }

    void Events::PropDestroyed(std::uint64_t propNetworkId) {
        EmitPropEvent("propDestroy", propNetworkId);
    }

    void Events::PropRested(std::uint64_t propNetworkId, const glm::vec3 &position, const glm::quat &rotation) {
        EmitReservedEvent("propRest", [propNetworkId, position, rotation](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapProp(isolate, propNetworkId));
            args.push_back(v8pp::convert<glm::vec3>::to_v8(isolate, position));
            args.push_back(v8pp::convert<glm::quat>::to_v8(isolate, rotation));
            return args;
        });
    }
} // namespace KCDC::Scripting
