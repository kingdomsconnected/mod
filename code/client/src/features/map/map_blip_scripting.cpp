#include "map_blip_scripting.h"

#include "core/application.h"

#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {
    std::unique_ptr<v8pp::class_<Blip>> Blip::_class;

    namespace {
        KCDC::Features::Map::MapBlipService *Blips() {
            return Core::gApplication ? &Core::gApplication->GetMapBlipService() : nullptr;
        }

        // The registry row behind a handle, or nothing once it is removed. A row the server owns is not a script's to see, so it reads as nothing too.
        bool Describe(std::uint32_t id, KCDC::Features::Map::BlipView &view) {
            KCDC::Features::Map::MapBlipService *blips = Blips();
            return blips != nullptr && id != 0 && blips->Describe(id, view) && !view.fromServer;
        }

        // The registry, only when `id` is a row a script may change.
        KCDC::Features::Map::MapBlipService *Owned(std::uint32_t id) {
            KCDC::Features::Map::BlipView view;
            return Describe(id, view) ? Blips() : nullptr;
        }

        // An optional boolean field of an options bag; absent or mistyped keeps `fallback`.
        bool ReadFlag(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, bool fallback) {
            v8::Local<v8::Value> value;
            if (!options->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) || !value->IsBoolean()) {
                return fallback;
            }
            return v8pp::from_v8<bool>(isolate, value);
        }

        // The handle a prototype function was called on, or nullptr.
        Blip *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Blip>::unwrap_object(info.GetIsolate(), info.This());
        }

        bool ReadPosition(v8::Isolate *isolate, v8::Local<v8::Object> options, glm::vec3 &position) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            v8::Local<v8::Value> value;
            if (!options->Get(context, v8pp::to_v8(isolate, "position")).ToLocal(&value) || !value->IsObject()) {
                return false;
            }

            position = v8pp::convert<glm::vec3>::from_v8(isolate, value);
            return true;
        }

        // JS: blip.remove()
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Blip *self                                 = Self(info);
            KCDC::Features::Map::MapBlipService *blips = self != nullptr ? Owned(self->GetId()) : nullptr;
            info.GetReturnValue().Set(blips != nullptr && blips->Remove(self->GetId()));
        }

        // JS: Blip.create({ position, type?, state?, label?, compass?, map?, legend? }) -> Blip | null
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: expected ({ position, type?, state?, label?, compass?, map?, legend? })"));
                return;
            }

            v8::Local<v8::Object> options  = info[0].As<v8::Object>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            KCDC::Features::Map::BlipView requested;
            if (!ReadPosition(isolate, options, requested.position)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: position is required and must be a Vector3"));
                return;
            }

            v8::Local<v8::Value> value;
            if (options->Get(context, v8pp::to_v8(isolate, "type")).ToLocal(&value) && value->IsString()) {
                const std::string name = v8pp::from_v8<std::string>(isolate, value);
                if (!KCDC::Features::Map::MapBlipService::IsKnownType(name)) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: " + name + " is not one of the game's compass mark types"));
                    return;
                }
                requested.type = name;
            }
            if (options->Get(context, v8pp::to_v8(isolate, "state")).ToLocal(&value) && value->IsNumber()) {
                requested.state = static_cast<std::int32_t>(v8pp::from_v8<double>(isolate, value));
            }
            if (options->Get(context, v8pp::to_v8(isolate, "label")).ToLocal(&value) && value->IsString()) {
                requested.label = v8pp::from_v8<std::string>(isolate, value);
            }
            requested.compass = ReadFlag(isolate, options, "compass", true);
            requested.map     = ReadFlag(isolate, options, "map", true);
            requested.legend  = ReadFlag(isolate, options, "legend", true);

            KCDC::Features::Map::MapBlipService *blips = Blips();
            const std::uint32_t id           = blips != nullptr ? blips->Create(requested) : 0;
            if (id == 0) {
                info.GetReturnValue().SetNull();
                return;
            }

            info.GetReturnValue().Set(v8pp::class_<Blip>::create_object(isolate, static_cast<double>(id)));
        }

        // JS: Blip.getById(id) -> Blip | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.getById: expected (id)"));
                return;
            }

            const auto id = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[0]));

            KCDC::Features::Map::BlipView view;
            if (!Describe(id, view)) {
                info.GetReturnValue().SetNull();
                return;
            }

            info.GetReturnValue().Set(v8pp::class_<Blip>::create_object(isolate, static_cast<double>(id)));
        }

        // JS: Blip.all() -> Blip[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Map::MapBlipService *blips            = Blips();
            const std::vector<KCDC::Features::Map::BlipView> rows = blips != nullptr ? blips->List() : std::vector<KCDC::Features::Map::BlipView> {};

            v8::Local<v8::Array> result = v8::Array::New(isolate);
            std::uint32_t index         = 0;
            for (const KCDC::Features::Map::BlipView &row : rows) {
                if (row.fromServer) {
                    continue;
                }
                if (result->Set(isolate->GetCurrentContext(), index++, v8pp::class_<Blip>::create_object(isolate, static_cast<double>(row.id))).IsNothing()) {
                    break;
                }
            }

            info.GetReturnValue().Set(result);
        }

        // JS: Blip.removeAll()
        void JS_RemoveAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (KCDC::Features::Map::MapBlipService *blips = Blips()) {
                blips->RemoveAllLocal();
            }
        }
    } // namespace

    bool Blip::IsValid() const {
        KCDC::Features::Map::BlipView view;
        return Describe(_id, view);
    }

    bool Blip::IsLive() const {
        KCDC::Features::Map::BlipView view;
        return Describe(_id, view) && view.live;
    }

    glm::vec3 Blip::GetPosition() const {
        KCDC::Features::Map::BlipView view;
        if (!Describe(_id, view)) {
            return glm::vec3 {0.0F};
        }
        return view.position;
    }

    void Blip::SetPosition(glm::vec3 position) {
        if (KCDC::Features::Map::MapBlipService *blips = Owned(_id)) {
            blips->SetPosition(_id, position);
        }
    }

    std::string Blip::GetType() const {
        KCDC::Features::Map::BlipView view;
        if (!Describe(_id, view)) {
            return {};
        }
        return view.type;
    }

    void Blip::SetType(std::string type) {
        // A name the game does not know leaves the blip as it was, which is what the service reports; Blip.create is the path that throws.
        if (KCDC::Features::Map::MapBlipService *blips = Owned(_id)) {
            blips->SetType(_id, type);
        }
    }

    double Blip::GetState() const {
        KCDC::Features::Map::BlipView view;
        if (!Describe(_id, view)) {
            return 0.0;
        }
        return static_cast<double>(view.state);
    }

    void Blip::SetState(double state) {
        if (KCDC::Features::Map::MapBlipService *blips = Owned(_id)) {
            blips->SetState(_id, static_cast<std::int32_t>(state));
        }
    }

    std::string Blip::GetLabel() const {
        KCDC::Features::Map::BlipView view;
        if (!Describe(_id, view)) {
            return {};
        }
        return view.label;
    }

    void Blip::SetLabel(std::string label) {
        if (KCDC::Features::Map::MapBlipService *blips = Owned(_id)) {
            blips->SetLabel(_id, label);
        }
    }

    bool Blip::GetCompass() const {
        KCDC::Features::Map::BlipView view;
        return Describe(_id, view) && view.compass;
    }

    void Blip::SetCompass(bool compass) {
        KCDC::Features::Map::BlipDescription description;
        KCDC::Features::Map::MapBlipService *blips = Owned(_id);
        if (blips != nullptr && blips->Find(_id, description)) {
            description.compass = compass;
            blips->SetDescription(_id, description);
        }
    }

    bool Blip::GetMap() const {
        KCDC::Features::Map::BlipView view;
        return Describe(_id, view) && view.map;
    }

    void Blip::SetMap(bool map) {
        KCDC::Features::Map::BlipDescription description;
        KCDC::Features::Map::MapBlipService *blips = Owned(_id);
        if (blips != nullptr && blips->Find(_id, description)) {
            description.map = map;
            blips->SetDescription(_id, description);
        }
    }

    bool Blip::GetLegend() const {
        KCDC::Features::Map::BlipView view;
        return Describe(_id, view) && view.legend;
    }

    void Blip::SetLegend(bool legend) {
        KCDC::Features::Map::BlipDescription description;
        KCDC::Features::Map::MapBlipService *blips = Owned(_id);
        if (blips != nullptr && blips->Find(_id, description)) {
            description.legend = legend;
            blips->SetDescription(_id, description);
        }
    }

    std::string Blip::ToString() const {
        KCDC::Features::Map::BlipView view;
        if (!Describe(_id, view)) {
            return "Blip{ id: " + std::to_string(_id) + ", removed }";
        }

        std::ostringstream ss;
        ss << "Blip{ id: " << _id << ", type: " << view.type << ", position: (" << view.position.x << ", " << view.position.y << ", " << view.position.z << ") }";
        return ss.str();
    }

    v8pp::class_<Blip> &Blip::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<Blip>>(isolate, ClientCatalog(), "Blip", "A mark this client owns on the game's compass and map screen.");
            _class->auto_wrap_objects(true);

            _class->ctor<double>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Identifier of an existing blip.")}, "Creates a handle for a blip that already exists; use Blip.create() to make one."));
            _class->function("toString", &Blip::ToString, v8pp::metadata::docs("string", {}, "Formats this blip handle for logging and debugging.", "The blip's id, its type and its position, or a note that it is gone."));

            _class->prototype_function("remove", &JS_Remove, v8pp::metadata::docs("boolean", {}, "Takes this blip off the compass and the map screen and out of the registry.", "True when it was still there to remove."));

            _class->property("id", &Blip::GetIdNumber, v8pp::metadata::property_docs("number", "Identifier of this blip, unique for the lifetime of the session. Ids are not reused."));
            _class->property("valid", &Blip::IsValid, v8pp::metadata::property_docs("boolean", "Whether the blip is still in the registry. A handle to a removed blip keeps reading, and every other property reads as its default."));
            _class->property("live", &Blip::IsLive, v8pp::metadata::property_docs("boolean", "Whether the compass currently carries a mark for this blip. It goes false on its own whenever the game empties the compass -- a level load does -- and back to true on the next tick."));

            _class->property("position", &Blip::GetPosition, &Blip::SetPosition, v8pp::metadata::property_docs("Vector3", "World-space position in metres. The compass recomputes bearing and distance from it every frame, so assigning moves the mark with no rebuild."));
            _class->property("type", &Blip::GetType, &Blip::SetType,
                v8pp::metadata::property_docs("string", "Which of the game's own compass icons the mark draws as, by the game's own name for it. Assignment rebuilds the native mark, because the icon is read once when a mark is registered and never re-read."));
            _class->property("state", &Blip::GetState, &Blip::SetState, v8pp::metadata::property_docs("number", "The mark's state, forwarded to the compass movie as-is. What a given value draws is undocumented; 0 is what a fresh mark carries."));
            _class->property("label", &Blip::GetLabel, &Blip::SetLabel,
                v8pp::metadata::property_docs("string", "The blip's name on the map screen, shown verbatim in place of the name the game gives its icon type; empty keeps that name. A compass mark has no text, so the compass never shows it."));
            _class->property("compass", &Blip::GetCompass, &Blip::SetCompass, v8pp::metadata::property_docs("boolean", "Whether the blip is drawn on the HUD compass. True by default."));
            _class->property("map", &Blip::GetMap, &Blip::SetMap,
                v8pp::metadata::property_docs("boolean",
                    "Whether the blip is drawn on the map screen. True by default, but only for an icon the map has art for: `Main`, `Side`, `Micro`, `Checkpoint`, `Dlcs`, `Bailiff`, `FistFight` and `Racing` have none and stay on the compass. The map legend's switch for the blip's icon type hides it like one of the game's own unless `legend` is false, and a change to a blip on the open map is redrawn within a frame."));
            _class->property("legend", &Blip::GetLegend, &Blip::SetLegend,
                v8pp::metadata::property_docs("boolean",
                    "Whether the map legend's switch for the blip's icon type hides it, as it does the game's own marks of that type. True by default. False keeps it on the map screen whatever that switch says, so a script can hide the game's own marks of a type with `WorldMap.setCategoryVisible` and still draw its own with the same icon."));

            _class->static_function("create", &JS_Create,
                v8pp::metadata::docs("Blip | null",
                    {v8pp::metadata::param("options", "{ position: Vector3; type?: string; state?: number; label?: string; compass?: boolean; map?: boolean; legend?: boolean }", false,
                        "Where the blip goes, and optionally which icon to draw it with, what state to give it, what the map screen calls it, whether it shows on the compass and the map screen -- both, by default -- and whether the map legend's switch for its icon hides it, as by default. `type` is one of the game's own compass mark names -- `Checkpoint`, `QuestGiver`, `Shop`, `GeneralPoi` and the rest -- and defaults to `GeneralPoi`; an unknown one is rejected rather than guessed at.")},
                    "Puts a mark on this client's compass and map screen and keeps it there, rebuilding it whenever the game drops it.", "The new blip, or null at the blip limit."));
            _class->static_function("getById", &JS_GetById, v8pp::metadata::docs("Blip | null", {v8pp::metadata::param("id", "number", false, "Blip identifier.")}, "Looks a blip up by its id.", "The blip's handle, or null when it no longer exists."));
            _class->static_function("all", &JS_All, v8pp::metadata::docs("Blip[]", {}, "Lists every blip this client's scripts made. Blips the server placed are not included.", "One handle per live blip, in creation order."));
            _class->static_function("removeAll", &JS_RemoveAll, v8pp::metadata::docs("void", {}, "Takes every blip this client's scripts made off the compass and the map screen, leaving the server's blips and the game's own marks alone."));
        }
        return *_class;
    }

    void Blip::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        GetClass(isolate).publish(global);
    }
} // namespace KCDC::Scripting
