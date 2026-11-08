#include "gate_scripting.h"

#include "core/scripting/wrap.h"
#include "core/server.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>

namespace KCDC::Scripting {

    namespace {
        // Unguarded on purpose: PostInit installs the gate service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::Door::GateService &ResolveGates() {
            return Server::Get()->GetGates();
        }

        // The handle a prototype function was called on, or nullptr.
        Gate *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Gate>::unwrap_object(info.GetIsolate(), info.This());
        }

        // The virtual world an argument names, or the global one every body starts in.
        MafiaNet::VirtualWorldId WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return MafiaNet::VIRTUAL_WORLD_GLOBAL;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // JS: gate.toggle(open) -> GateToggle
        void JS_Toggle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            auto *self = Self(info);
            auto *gate = self != nullptr ? self->ResolveGate() : nullptr;
            if (gate == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "gate.toggle: this gate is no longer on the server"));
                return;
            }

            // Anything but a boolean is not a direction, and means the same as none.
            std::optional<bool> direction;
            if (info.Length() >= 1 && info[0]->IsBoolean()) {
                direction = info[0]->BooleanValue(isolate);
            }

            const KCDC::Features::Door::GateService::ToggleResult result = ResolveGates().Toggle(*gate, direction);

            v8::Local<v8::Object> answer = v8::Object::New(isolate);
            answer->Set(context, v8pp::to_v8(isolate, "accepted"), v8pp::to_v8(isolate, result.accepted)).Check();
            answer->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, result.reason)).Check();
            answer->Set(context, v8pp::to_v8(isolate, "opening"), v8pp::to_v8(isolate, result.opening)).Check();
            answer->Set(context, v8pp::to_v8(isolate, "seconds"), v8pp::to_v8(isolate, result.seconds)).Check();
            info.GetReturnValue().Set(answer);
        }

        // JS: Gate.all() -> Gate[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> gates     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *gate : ResolveGates().Collect(std::nullopt)) {
                gates->Set(context, index++, WrapGate(isolate, gate->GetNetworkID())).Check();
            }

            info.GetReturnValue().Set(gates);
        }

        // JS: Gate.getById(id) -> Gate | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Gate.getById: expected (id)"));
                return;
            }

            info.GetReturnValue().Set(WrapGate(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: Gate.find(guid, virtualWorld) -> Gate | null
        void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Gate.find: expected (guid, virtualWorld?)"));
                return;
            }

            const std::uint64_t guid = ParseLevelGuid(v8pp::from_v8<std::string>(isolate, info[0]));
            if (guid == 0) {
                info.GetReturnValue().SetNull();
                return;
            }

            Shared::Entities::GateEntity *gate = ResolveGates().Resolve(guid, WorldArg(info, 1));
            info.GetReturnValue().Set(gate != nullptr ? WrapGate(isolate, gate->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Gate.nearest(position, radius, virtualWorld) -> Gate | null
        void JS_Nearest(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 2 || !info[0]->IsObject() || !info[1]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Gate.nearest: expected (position, radius, virtualWorld?)"));
                return;
            }

            // Called directly for the reason ParseRotationArg gives.
            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const float radius       = static_cast<float>(v8pp::from_v8<double>(isolate, info[1]));

            Shared::Entities::GateEntity *gate = ResolveGates().Nearest(position, WorldArg(info, 2), radius);
            info.GetReturnValue().Set(gate != nullptr ? WrapGate(isolate, gate->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }
    } // namespace

    Shared::Entities::GateEntity *Gate::ResolveGate() const {
        return dynamic_cast<Shared::Entities::GateEntity *>(Resolve());
    }

    std::string Gate::GetGuid() const {
        auto *gate = ResolveGate();
        return FormatLevelGuid(gate != nullptr ? gate->gateGuid : 0);
    }

    std::string Gate::GetKind() const {
        auto *gate = ResolveGate();
        return gate != nullptr ? KCDC::Features::Door::GateService::Name(*gate) : std::string();
    }

    std::string Gate::GetCycle() const {
        auto *gate = ResolveGate();
        if (gate == nullptr) {
            return std::string();
        }
        switch (gate->state) {
        case Shared::Entities::GateState::Closed: return "closed";
        case Shared::Entities::GateState::Opening: return "opening";
        case Shared::Entities::GateState::Open: return "open";
        case Shared::Entities::GateState::Closing: return "closing";
        }
        return std::string();
    }

    float Gate::GetOpenness() const {
        auto *gate = ResolveGate();
        return gate != nullptr ? KCDC::Features::Door::GateService::Openness(*gate) : 0.0F;
    }

    bool Gate::IsMoving() const {
        auto *gate = ResolveGate();
        return gate != nullptr && Shared::Entities::IsGateMoving(gate->state);
    }

    float Gate::GetOpenDuration() const {
        auto *gate = ResolveGate();
        return gate != nullptr ? static_cast<float>(gate->Asset().openDurationMs) / 1000.0F : 0.0F;
    }

    float Gate::GetCloseDuration() const {
        auto *gate = ResolveGate();
        return gate != nullptr ? static_cast<float>(gate->Asset().closeDurationMs) / 1000.0F : 0.0F;
    }

    std::string Gate::ToString() const {
        std::ostringstream ss;
        ss << "Gate{ id: " << GetId() << ", guid: " << GetGuid() << ", kind: " << GetKind() << ", cycle: " << GetCycle() << " }";
        return ss.str();
    }

    v8pp::class_<Gate> &Gate::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Gate>>(isolate, ServerCatalog(), "Gate", "Replicated handle for one of the level's castle gates.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for a gate the server already knows; gates are the level's own and are never spawned."))
                .function("toString", &Gate::ToString, v8pp::metadata::docs("string", {}, "Formats this gate handle for logging and debugging.", "The gate ID, its level GUID, what kind it is and where it is in its cycle."));

            _class->property("guid", &Gate::GetGuid, v8pp::metadata::property_docs("string", "The level's own EntityGuid, as sixteen lowercase hex digits. The same on every machine, so it is the identity to store a gate under; `Gate.find` takes it back. A `gate` has no entity of its own, so its key is the GUID of the layer that draws it shut."));
            _class->property("kind", &Gate::GetKind, v8pp::metadata::property_docs("string", "What piece of architecture this is: `drawbridge` or `portcullis`, which animate, or `gate`, a pair of leaves the level swaps between drawn open and drawn shut -- the Ratborsch and Nebakov fortress gates and the Ruthard palace gate. A `gate` opens and closes at once."));
            _class->property("cycle", &Gate::GetCycle,
                v8pp::metadata::property_docs("string",
                    "Where the gate is in its cycle: `closed`, `opening`, `open` or `closing`. Open always means passable -- a portcullis raised, a drawbridge lowered. Named `cycle` because every entity already has a `state`, which is its state bag."));
            _class->property("openness", &Gate::GetOpenness,
                v8pp::metadata::property_docs("number",
                    "How far open the gate is, from 0 shut to 1 fully open, read off the pose curve mined from the asset's own animation at the point the server clock says the motion has reached. It follows the bars, not the clock: a portcullis reads 0 within the first fifth of its close, and holds there while the clip finishes."));
            _class->property("moving", &Gate::IsMoving, v8pp::metadata::property_docs("boolean", "Whether a cycle is running. `toggle` reverses a moving gate from where it has got to rather than refusing it."));
            _class->property("openDuration", &Gate::GetOpenDuration,
                v8pp::metadata::property_docs("number", "How long opening takes, in seconds, from the key range the asset's animation database stores. A gate with no opening clip runs its closing one backwards, so this is that clip's length. 0 for a `gate`."));
            _class->property("closeDuration", &Gate::GetCloseDuration, v8pp::metadata::property_docs("number", "How long closing takes, in seconds, from the asset's animation database."));

            _class->prototype_function("toggle", &JS_Toggle,
                v8pp::metadata::docs("GateToggle", {v8pp::metadata::param("open", "boolean", true, "true opens, false closes. Omitted heads away from whichever end the gate is at, reversing one already moving.")},
                    "Starts a cycle. The motion is never streamed: every client plays it out from the server clock it started on, and a gate reversed mid-cycle picks up where it is rather than snapping to the far end.",
                    "What happened, and the phrase to explain it with when nothing did."));
        }
        return *_class;
    }

    void Gate::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Gate::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &toggle = ServerCatalog().data_type("GateToggle");
        toggle.add_property("accepted", "boolean", "Whether a cycle started.");
        toggle.add_property("reason", "string", "Why nothing moved, as a phrase that reads after the gate's name: `is already open`, `is already closing`. Empty when a cycle started.");
        toggle.add_property("opening", "boolean", "The direction taken, or the one that was refused.");
        toggle.add_property("seconds", "number", "How long the motion still has to run. 0 when nothing moved.");

        v8pp::class_<Gate> &cls = GetClass(isolate);

        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Gate[]", {}, "Lists every gate the level places, all of which exist from boot in the global world. In another virtual world a gate is built the first time anything there reaches it, so this lists only those.",
                "One handle per gate, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Gate | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a gate up by its network entity ID.", "The gate's handle, or null when no live gate has that ID."));
        cls.static_function("find", &JS_Find,
            v8pp::metadata::docs("Gate | null",
                {
                    v8pp::metadata::param("guid", "string", false, "The gate's level EntityGuid as hex, with or without an `0x` prefix, as `gate.guid` prints it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one, where every body starts."),
                },
                "Looks a gate up by the level's own identity for it, which survives a restart and is the same on every machine. A gate the level places is always found, in any virtual world.", "The gate's handle, or null when the level places no such gate."));
        cls.static_function("nearest", &JS_Nearest,
            v8pp::metadata::docs("Gate | null",
                {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "World-space point to measure from."),
                    v8pp::metadata::param("radius", "number", false, "How far to look, in metres. Castle architecture is visible from a long way off, so this is tens of metres rather than a couple."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one."),
                },
                "Finds the gate nearest a point. Gate positions come from the level's own data, so unlike a door this needs no round trip to anybody's client.", "The nearest gate within the radius, or null when there is none."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
