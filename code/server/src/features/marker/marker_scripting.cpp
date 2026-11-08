#include "marker_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/marker/marker_verbs.h"

#include "shared/game/marker_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/marker_options.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape both of this feature's events have: one subject, nothing else.
        void EmitMarkerEvent(const char *eventName, std::uint64_t markerNetworkId) {
            EmitReservedEvent(eventName, [markerNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapMarker(isolate, markerNetworkId));
                return args;
            });
        }

        // The shape this feature's crossing events have: a marker and whoever crossed it.
        void EmitCrossingEvent(const char *eventName, std::uint64_t markerNetworkId, std::uint64_t playerNetworkId) {
            EmitReservedEvent(eventName, [markerNetworkId, playerNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapMarker(isolate, markerNetworkId));
                args.push_back(WrapPlayer(isolate, playerNetworkId));
                return args;
            });
        }

        /** The virtual world an argument names, or none for every one of them. */
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        /** The handle a prototype function was called on, or nullptr. */
        Marker *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Marker>::unwrap_object(info.GetIsolate(), info.This());
        }

        /** The catalog row a live handle stands for, or null once it has gone. */
        const Shared::Game::MarkerMaterialEntry *RowOf(const Marker &handle) {
            auto *marker = handle.ResolveMarker();
            return marker != nullptr ? Shared::Game::MarkerMaterialAt(marker->materialIndex) : nullptr;
        }

        /** The catalog row a name argument means, throwing the binding's own error when it names nothing. */
        std::size_t MaterialArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            v8::Isolate *isolate    = info.GetIsolate();
            const std::string given = info.Length() > 0 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            const std::size_t index = Shared::Game::FindMarkerMaterialIndex(given);
            if (index == Shared::Game::kNoMarkerMaterial) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": \"" + given + "\" is not a material in the marker catalog"));
            }
            return index;
        }

        // JS: marker.remove()
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Marker::Markers::Destroy(self->ResolveMarker());
            }
        }

        // JS: Marker.place(material, position, options?, virtualWorld?) -> Marker
        void JS_Place(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            const std::size_t materialIndex = MaterialArg(info, "Marker.place");
            if (materialIndex == Shared::Game::kNoMarkerMaterial) {
                return;
            }

            glm::vec3 position {0.0F};
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }

            const MarkerOptions options      = ParseMarkerOptions(info, 2);
            const std::uint32_t virtualWorld = info.Length() >= 4 && info[3]->IsNumber() ? static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[3])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;

            auto *marker = KCDC::Features::Marker::Markers::Place(materialIndex, options.shape, position, options.rotation, options.size, options.height, options.spin, options.bob, options.color, options.trigger, virtualWorld);
            if (marker == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Marker.place: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapMarker(isolate, marker->GetNetworkID()));
        }

        // JS: Marker.all(virtualWorld?) -> Marker[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> markers   = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *marker : KCDC::Features::Marker::Markers::Collect(WorldArg(info, 0))) {
                markers->Set(context, index++, WrapMarker(isolate, marker->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(markers);
        }

        // JS: Marker.getById(id) -> Marker | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Marker.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapMarker(isolate, networkId));
        }

        // JS: Marker.removeAll(virtualWorld?) -> number
        void JS_RemoveAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Marker::Markers::DestroyAll(WorldArg(info, 0))));
        }

        // JS: Marker.list(prefix?) -> string[]
        void JS_List(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            const std::string prefix = info.Length() > 0 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            v8::Local<v8::Array> names = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::MarkerMaterialEntry &entry : Shared::Game::g_MarkerMaterials) {
                if (!prefix.empty() && entry.name.rfind(prefix, 0) != 0) {
                    continue;
                }
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(entry.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }
    } // namespace

    Shared::Entities::MarkerEntity *Marker::ResolveMarker() const {
        return dynamic_cast<Shared::Entities::MarkerEntity *>(Resolve());
    }

    std::string Marker::GetMaterial() const {
        const Shared::Game::MarkerMaterialEntry *row = RowOf(*this);
        return row != nullptr ? std::string(row->name) : std::string();
    }

    std::string Marker::GetGroup() const {
        const Shared::Game::MarkerMaterialEntry *row = RowOf(*this);
        return row != nullptr ? std::string(row->group) : std::string();
    }

    std::string Marker::GetShape() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? std::string(Shared::Entities::MarkerShapeName(marker->GetShape())) : std::string();
    }

    float Marker::GetSize() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? marker->size : 0.0F;
    }

    void Marker::SetSize(float size) {
        if (auto *marker = ResolveMarker()) {
            KCDC::Features::Marker::Markers::SetExtent(marker, std::clamp(size, Shared::Entities::MarkerEntity::kMinSize, Shared::Entities::MarkerEntity::kMaxSize), marker->height);
        }
    }

    float Marker::GetHeight() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? marker->height : 0.0F;
    }

    void Marker::SetHeight(float height) {
        if (auto *marker = ResolveMarker()) {
            KCDC::Features::Marker::Markers::SetExtent(marker, marker->size, std::clamp(height, Shared::Entities::MarkerEntity::kMinHeight, Shared::Entities::MarkerEntity::kMaxHeight));
        }
    }

    float Marker::GetSpin() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? marker->spin : 0.0F;
    }

    void Marker::SetSpin(float spin) {
        if (auto *marker = ResolveMarker()) {
            KCDC::Features::Marker::Markers::SetMotion(marker, std::clamp(spin, -Shared::Entities::MarkerEntity::kMaxSpin, Shared::Entities::MarkerEntity::kMaxSpin), marker->bob);
        }
    }

    float Marker::GetBob() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? marker->bob : 0.0F;
    }

    void Marker::SetBob(float bob) {
        if (auto *marker = ResolveMarker()) {
            KCDC::Features::Marker::Markers::SetMotion(marker, marker->spin, std::clamp(bob, 0.0F, Shared::Entities::MarkerEntity::kMaxBob));
        }
    }

    double Marker::GetColor() const {
        auto *marker = ResolveMarker();
        return marker != nullptr ? static_cast<double>(marker->color) : 0.0;
    }

    void Marker::SetColor(double color) {
        if (auto *marker = ResolveMarker()) {
            const double bounded = std::clamp(color, 0.0, 4294967295.0);
            KCDC::Features::Marker::Markers::SetColor(marker, static_cast<std::uint32_t>(bounded));
        }
    }

    bool Marker::GetTrigger() const {
        auto *marker = ResolveMarker();
        return marker != nullptr && marker->trigger;
    }

    void Marker::SetTrigger(bool trigger) {
        if (auto *marker = ResolveMarker()) {
            marker->trigger = trigger;
        }
    }

    std::string Marker::ToString() const {
        std::ostringstream ss;
        ss << "Marker{ id: " << GetId() << ", shape: " << GetShape() << ", material: " << GetMaterial() << ", size: " << GetSize() << " }";
        return ss.str();
    }

    v8pp::class_<Marker> &Marker::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Marker>>(isolate, ServerCatalog(), "Marker", "Replicated ground marker handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing marker with this ID; use Marker.place() to draw one."))
                .function("toString", &Marker::ToString, v8pp::metadata::docs("string", {}, "Formats this marker handle for logging and debugging.", "The marker ID, its material and its size."));

            _class->prototype_function("remove", &JS_Remove, v8pp::metadata::docs("void", {}, "Removes this marker from every client that can see it, after emitting markerRemove."));

            _class->property("material", &Marker::GetMaterial,
                v8pp::metadata::property_docs("string", "Decal material this marker is drawn with, e.g. `materials/decals/chalk_cross`. Read-only: a decal node reads its material once, so a marker is the one it was placed as."));
            _class->property("group", &Marker::GetGroup, v8pp::metadata::property_docs("string", "Subfolder the material came out of under `materials/decals/`, empty at the top level."));
            _class->property("shape", &Marker::GetShape,
                v8pp::metadata::property_docs("string", "What the marker is made of: `decal` for the projected one, or `cylinder`, `sphere`, `chevron`, `cube` or `plane` for a mesh standing in the world. Read-only: changing it is a rebuild, so place a new marker instead."));
            _class->property("size", &Marker::GetSize, &Marker::SetSize,
                v8pp::metadata::property_docs("number", "How wide the marker is, in metres, from 0.25 to 64; a value outside that is clamped into it. Assignment restates it in place rather than rebuilding, and widens how far the marker streams."));
            _class->property("height", &Marker::GetHeight, &Marker::SetHeight,
                v8pp::metadata::property_docs("number", "How tall a mesh marker stands, in metres, from 0.25 to 64. Ignored by `decal`, `sphere` and `plane`, which are sized by width alone."));
            _class->property("spin", &Marker::GetSpin, &Marker::SetSpin,
                v8pp::metadata::property_docs("number", "Degrees per second the marker turns about its own up axis, from -720 to 720; 0 stands still. Played on each client's own clock, so two players need not see it at the same angle. Ignored by `decal`."));
            _class->property("color", &Marker::GetColor, &Marker::SetColor,
                v8pp::metadata::property_docs("number", "Packed 0xRRGGBBAA tint, or 0 for the material as it ships. The shipped material is shared by everything that names it, so a tint is a private clone rather than a write to the original -- which is why assignment rebuilds the marker."));
            _class->property("trigger", &Marker::GetTrigger, &Marker::SetTrigger,
                v8pp::metadata::property_docs("boolean", "Whether clients watch the local player against this marker's volume and report crossings, which is what raises `markerEnter` and `markerExit`. Off by default: a marker nobody listens to should cost nothing but its draw."));
            _class->property("bob", &Marker::GetBob, &Marker::SetBob,
                v8pp::metadata::property_docs("number", "How far the marker rises and falls from its resting height, in metres, up to 4; 0 stands still. Played on each client's own clock. Ignored by `decal`, which cannot leave the surface it projects onto."));
        }
        return *_class;
    }

    void Marker::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Marker::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("markerPlace", "[marker: Marker]", "Dispatched immediately after a ground marker is drawn, whether by `Marker.place`, a command, or anything else.");
        events.add_property("markerRemove", "[marker: Marker]", "Dispatched while a ground marker is being removed. The handle still resolves, so its material and its pose can be read one last time.");
        events.add_property("markerEnter", "[marker: Marker, player: Player]", "Dispatched when a player walks into a marker whose `trigger` is on. The client detects the crossing and the server confirms it against the position it already replicates, so a claim it does not agree with never reaches here.");
        events.add_property("markerExit", "[marker: Marker, player: Player]", "Dispatched when a player walks out of a marker whose `trigger` is on. Not raised when the marker is removed, when `trigger` is turned off, or when the player leaves the world -- none of those is the player walking out.");

        v8pp::class_<Marker> &cls = GetClass(isolate);

        cls.static_function("place", &JS_Place,
            v8pp::metadata::docs("Marker",
                {
                    v8pp::metadata::param("material", "string", false, "Decal material to project, spelled as the game's own data spells it -- `materials/decals/chalk_cross`. `Marker.list()` is the whole vocabulary."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space position; omitted components default to zero."),
                    v8pp::metadata::param("options", kMarkerOptionsType, true,
                        "`shape` picks what it is made of -- `decal` projects onto the surface under it, the rest stand in the world as meshes and can be seen from below a ridge. "
                        "`rotation` aims it, so a marker laid on a wall is the same call with a different rotation; `size` and `height` are its extent in metres; `spin` and `bob` give a mesh marker motion; `color` tints it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the marker belongs to; omitted puts it in the global one."),
                },
                "Puts a marker in the world and leaves it there. It is a replicated entity, so the interest grid streams it to whoever comes near and takes it away again when they leave -- which is what a zone or a checkpoint needs and what a one-shot message cannot do. "
                "How far it streams is taken from its own extent, between 50 and 400 metres.",
                "The newly drawn marker handle."));

        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Marker[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every replicated marker the server currently has.",
                "One handle per live marker, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Marker | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a replicated marker up by its network entity ID.", "The marker's handle, or null when no live marker has that ID."));
        cls.static_function("removeAll", &JS_RemoveAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Removes markers, emitting markerRemove for each one.", "How many markers were removed."));
        cls.static_function("list", &JS_List,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("prefix", "string", true, "Keep only the names starting with this, e.g. `materials/decals/burglar`.")},
                "Every decal material the shipped game preloads, in order. Mined from the game's own material folder at build time, so it is the same list the client has.", "The matching names."));
        cls.publish(global);
    }

    void Events::MarkerPlaced(std::uint64_t markerNetworkId) {
        EmitMarkerEvent("markerPlace", markerNetworkId);
    }

    void Events::MarkerRemoved(std::uint64_t markerNetworkId) {
        EmitMarkerEvent("markerRemove", markerNetworkId);
    }

    void Events::MarkerEntered(std::uint64_t markerNetworkId, std::uint64_t playerNetworkId) {
        EmitCrossingEvent("markerEnter", markerNetworkId, playerNetworkId);
    }

    void Events::MarkerExited(std::uint64_t markerNetworkId, std::uint64_t playerNetworkId) {
        EmitCrossingEvent("markerExit", markerNetworkId, playerNetworkId);
    }
} // namespace KCDC::Scripting
