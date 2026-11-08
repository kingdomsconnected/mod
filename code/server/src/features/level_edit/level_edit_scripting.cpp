#include "level_edit_scripting.h"

#include "features/level_edit/level_edit_verbs.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/rotation_arg.h"
#include "shared/scripting/virtual_world_arg.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The handle a prototype function was called on, or nullptr.
        LevelEdit *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<LevelEdit>::unwrap_object(info.GetIsolate(), info.This());
        }

        // A world a script named, or the global one.
        std::uint32_t WorldOrGlobal(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return VirtualWorldArg(info, index).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        }

        // One definition out of a script value. A Vector3 and a Quaternion stringify to their components, so the map's arrays and a script's own values both come down to the one reader the editor's files go through.
        bool ReadDefinition(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::LevelEditDefinition &definition, std::string &error) {
            const std::optional<std::string> text = value.IsEmpty() || !value->IsObject() ? std::nullopt : StringifyValue(isolate, value);
            if (!text.has_value()) {
                error = "expected a LevelEditDefinition object";
                return false;
            }
            return Shared::Game::LevelEditDefinitionFromJson(nlohmann::json::parse(*text, nullptr, false), definition, error);
        }

        v8::Local<v8::Array> EditsToArray(v8::Isolate *isolate, const std::vector<Shared::Entities::LevelEditEntity *> &edits) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> array     = v8::Array::New(isolate, static_cast<int>(edits.size()));
            std::uint32_t index            = 0;
            for (Shared::Entities::LevelEditEntity *edit : edits) {
                array->Set(context, index++, WrapLevelEdit(isolate, edit->GetNetworkID())).Check();
            }
            return array;
        }

        // JS: LevelEdit.apply(definition, virtualWorld?) -> LevelEdit
        void JS_Apply(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Shared::Game::LevelEditDefinition definition;
            std::string error;
            if (!ReadDefinition(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), definition, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "LevelEdit.apply: " + error));
                return;
            }

            auto *edit = KCDC::Features::LevelEdit::LevelEdits::Apply(definition, WorldOrGlobal(info, 1), error);
            if (edit == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "LevelEdit.apply: " + error));
                return;
            }
            info.GetReturnValue().Set(WrapLevelEdit(isolate, edit->GetNetworkID()));
        }

        // JS: LevelEdit.all(virtualWorld?) -> LevelEdit[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            info.GetReturnValue().Set(EditsToArray(isolate, KCDC::Features::LevelEdit::LevelEdits::Collect(VirtualWorldArg(info, 0))));
        }

        // JS: LevelEdit.getById(id) -> LevelEdit | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "LevelEdit.getById: expected (id)"));
                return;
            }
            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapLevelEdit(isolate, networkId));
        }

        // JS: LevelEdit.restoreAll(virtualWorld?) -> number
        void JS_RestoreAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::LevelEdit::LevelEdits::RestoreAll(VirtualWorldArg(info, 0))));
        }

        // JS: edit.move(position, rotation?, scale?) -> boolean
        void JS_Move(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self = Self(info);
            auto *edit = self != nullptr ? self->Resolve() : nullptr;

            if (edit == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                info.GetReturnValue().Set(false);
                return;
            }
            // Read the way every spawn verb reads its pose, called directly for the reason ParseRotationArg gives. Left out, the rotation and scale stay as the last move had them; Move refuses anything not finite.
            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const glm::quat rotation = info.Length() > 1 ? ParseRotationArg(isolate, info[1], edit->movedRotation) : edit->movedRotation;
            glm::vec3 scale          = edit->movedScale;
            if (info.Length() > 2 && info[2]->IsNumber()) {
                scale = glm::vec3(static_cast<float>(v8pp::from_v8<double>(isolate, info[2])));
            }
            else if (info.Length() > 2 && info[2]->IsObject()) {
                scale = v8pp::convert<glm::vec3>::from_v8(isolate, info[2]);
            }
            info.GetReturnValue().Set(KCDC::Features::LevelEdit::LevelEdits::Move(edit, position, rotation, scale));
        }

        // JS: edit.clearMove()
        void JS_ClearMove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::LevelEdit::LevelEdits::ClearMove(self->Resolve());
            }
        }

        // JS: edit.restore()
        void JS_Restore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::LevelEdit::LevelEdits::Restore(self->Resolve());
            }
        }

        // JS: edit.toJSON() -> LevelEditDefinition | null
        void JS_ToJSON(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self = Self(info);
            auto *edit = self != nullptr ? self->Resolve() : nullptr;
            if (edit == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            // The shared writer, so what a script saves is exactly a map's `world` entry.
            info.GetReturnValue().Set(ParseJsonValue(isolate, Shared::Game::LevelEditDefinitionToJson(edit->Describe()).dump(), v8::Null(isolate)));
        }
    } // namespace

    Shared::Entities::LevelEditEntity *LevelEdit::Resolve() const {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && _networkId != 0 ? replication->GetEntity<Shared::Entities::LevelEditEntity>(_networkId) : nullptr;
    }

    bool LevelEdit::Exists() const {
        return Resolve() != nullptr;
    }

    std::string LevelEdit::GetKind() const {
        const auto *edit = Resolve();
        if (edit == nullptr) {
            return std::string();
        }
        return edit->kind == Shared::Game::LevelEditKind::Entity ? "entity" : "brush";
    }

    std::string LevelEdit::GetName() const {
        const auto *edit = Resolve();
        return edit != nullptr ? edit->name : std::string();
    }

    std::string LevelEdit::GetGuid() const {
        const auto *edit = Resolve();
        return edit != nullptr && edit->kind == Shared::Game::LevelEditKind::Entity ? FormatLevelGuid(edit->guid) : std::string();
    }

    double LevelEdit::GetVirtualWorld() const {
        const auto *edit = Resolve();
        return edit != nullptr ? static_cast<double>(edit->GetVirtualWorld()) : 0.0;
    }

    bool LevelEdit::IsHidden() const {
        const auto *edit = Resolve();
        return edit != nullptr && edit->hidden;
    }

    void LevelEdit::SetHidden(bool hidden) {
        KCDC::Features::LevelEdit::LevelEdits::SetHidden(Resolve(), hidden);
    }

    std::string LevelEdit::GetModel() const {
        const auto *edit = Resolve();
        return edit != nullptr ? edit->model : std::string();
    }

    void LevelEdit::SetModel(const std::string &model) {
        std::string error;
        if (auto *edit = Resolve(); edit != nullptr && !KCDC::Features::LevelEdit::LevelEdits::SetModel(edit, model, error)) {
            throw std::invalid_argument("levelEdit.model: " + error);
        }
    }

    bool LevelEdit::IsMoved() const {
        const auto *edit = Resolve();
        return edit != nullptr && edit->moved;
    }

    std::string LevelEdit::ToString() const {
        std::ostringstream ss;
        ss << "LevelEdit{ id: " << _networkId << ", kind: " << GetKind() << ", name: " << GetName() << ", hidden: " << (IsHidden() ? "true" : "false") << ", moved: " << (IsMoved() ? "true" : "false");
        if (const std::string model = GetModel(); !model.empty()) {
            ss << ", model: " << model;
        }
        ss << " }";
        return ss.str();
    }

    v8::Local<v8::Value> WrapLevelEdit(v8::Isolate *isolate, std::uint64_t levelEditNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || levelEditNetworkId == 0 || replication->GetEntity<Shared::Entities::LevelEditEntity>(levelEditNetworkId) == nullptr) {
            return v8::Null(isolate);
        }
        LevelEdit::GetClass(isolate);
        return v8pp::class_<LevelEdit>::create_object(isolate, levelEditNetworkId);
    }

    v8pp::class_<LevelEdit> &LevelEdit::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            _class = std::make_unique<v8pp::class_<LevelEdit>>(isolate, ServerCatalog(), "LevelEdit",
                "One change the server makes, for every player, to an object the level itself placed -- a brush, a tree or a level entity taken out of the world, moved, swapped for another mesh, or a mix of those. It is what a World Builder map's `world` array carries. `WorldRayHit.levelObject` names the object a trace met in the form `apply` takes.");
            _class->auto_wrap_objects(true);
            _class->ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Wraps an edit the server already holds; use LevelEdit.apply to make one, or WorldResource for a whole export."))
                .function("toString", &LevelEdit::ToString, v8pp::metadata::docs("string", {}, "Formats this edit for logging and debugging.", "Its id, kind, object and what it does to it."));

            _class->prototype_function("move", &JS_Move,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("position", "Vector3", false, "Where the object stands instead."),
                        v8pp::metadata::param("rotation", "Quaternion | Vector3", true, "Its orientation: a Quaternion, or Euler angles in degrees. Left out, it keeps the last move's."),
                        v8pp::metadata::param("scale", "number | Vector3", true, "A uniform scale or one per axis, from 0.01 to 100. Left out, it keeps the last move's."),
                    },
                    "Moves the object for every player, collision included.", "False for a gone edit or a transform that is not finite."));
            _class->prototype_function("clearMove", &JS_ClearMove, v8pp::metadata::docs("void", {}, "Puts the object back where the level placed it, keeping it out of the world if the edit takes it out."));
            _class->prototype_function("restore", &JS_Restore, v8pp::metadata::docs("void", {}, "Drops the edit: every player gets the object back exactly as the level had it, collision included."));
            _class->prototype_function("toJSON", &JS_ToJSON,
                v8pp::metadata::docs("LevelEditDefinition | null", {}, "The definition `LevelEdit.apply` takes to make this edit again, which is what a script saves: the server keeps nothing past a restart. It is one entry of a World Builder map's `world` array.",
                    "Null once the edit is gone."));

            _class->property("id", &LevelEdit::GetId, v8pp::metadata::property_docs("number", "Immutable network entity identifier."));
            _class->property("exists", &LevelEdit::Exists, v8pp::metadata::property_docs("boolean", "False once the edit has been restored; every other read then returns its empty value."));
            _class->property("kind", &LevelEdit::GetKind, v8pp::metadata::property_docs("'brush' | 'entity'", "Whether the object is a brush, found by its mesh and where the level put it, or a level entity, found by its EntityGuid."));
            _class->property("name", &LevelEdit::GetName, v8pp::metadata::property_docs("string", "A brush's mesh path, or the name the level gives the entity."));
            _class->property("guid", &LevelEdit::GetGuid, v8pp::metadata::property_docs("string", "An entity's level EntityGuid as sixteen lowercase hex digits; empty for a brush."));
            _class->property("virtualWorld", &LevelEdit::GetVirtualWorld, v8pp::metadata::property_docs("number", "The virtual world whose players see the edit, fixed when it was applied."));
            _class->property("hidden", &LevelEdit::IsHidden, &LevelEdit::SetHidden,
                v8pp::metadata::property_docs("boolean", "Whether the object is taken out of the world: not drawn and not solid. Assignment takes it out or puts it back for every player, keeping any move."));
            _class->property("moved", &LevelEdit::IsMoved, v8pp::metadata::property_docs("boolean", "Whether the object stands somewhere other than where the level placed it; `move` and `clearMove` change it."));
            _class->property("model", &LevelEdit::GetModel, &LevelEdit::SetModel,
                v8pp::metadata::property_docs("string",
                    "The mesh standing in the object's place, or empty. Assigning a prop catalog path or a mesh this server streams swaps the object for it on every player: the object goes, collision included, and the mesh stands where it stood -- or where `move` put it -- as solid static scenery. A felled tree leaves its stump this way. Assigning an empty string gives the object back. Throws for any other path."));
        }
        return *_class;
    }

    void LevelEdit::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void LevelEdit::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &definition = ServerCatalog().data_type("LevelEditDefinition");
        definition.add_property("kind", v8pp::metadata::type{"'brush' | 'entity' | undefined", {}, true}, "What the object is; a brush when omitted. A brush has no identity of its own, so it is found by its mesh and where the level put it; a level entity by its EntityGuid.");
        definition.add_property("name", v8pp::metadata::type{"string | undefined", {}, true}, "A brush's mesh path, `objects/...cgf`, as the World Builder shows it; required for a brush. An entity's name, kept for reading only.");
        definition.add_property("guid", v8pp::metadata::type{"string | undefined", {}, true}, "An entity's level EntityGuid as sixteen hex digits; required for an entity.");
        definition.add_property("class", v8pp::metadata::type{"string | undefined", {}, true}, "An entity's class, kept for reading only.");
        definition.add_property("at", "Vector3 | number[]", "Where the level put the object: its pivot, matched within 5 cm. A brush's key, with its mesh.");
        definition.add_property("hidden", v8pp::metadata::type{"boolean | undefined", {}, true}, "Whether it is taken out of the world: not drawn and not solid.");
        definition.add_property("model", v8pp::metadata::type {"string | undefined", {}, true},
            "A mesh that stands in its place: a prop catalog path such as `objects/natural/vegetation/trees/stumps/stump_fagus_a.cgf`, or a mesh this server streams. The object goes out of the world, collision included, and this mesh stands where it stood -- or at `position` -- as solid static scenery.");
        definition.add_property("position", v8pp::metadata::type{"Vector3 | number[] | undefined", {}, true}, "Where it stands instead; present, the edit is a move.");
        definition.add_property("rotation", v8pp::metadata::type{"Quaternion | number[] | undefined", {}, true}, "A move's orientation: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.");
        definition.add_property("scale", v8pp::metadata::type{"Vector3 | number[] | undefined", {}, true}, "A move's scale per axis, from 0.01 to 100. None when omitted.");

        v8pp::class_<LevelEdit> &cls = GetClass(isolate);
        cls.static_function("apply", &JS_Apply,
            v8pp::metadata::docs("LevelEdit",
                {
                    v8pp::metadata::param("definition", "LevelEditDefinition", false, "The object and what to do to it, in the format of one entry of a World Builder map's `world` array."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world whose players see it; the global one when omitted."),
                },
                "Takes an object the level placed out of the world, moves it, or swaps it for another mesh, for every player in the virtual world -- whenever they join, and however far away they are. Collision goes with it. An edit already made to the same object in that world is restated rather than doubled, so applying one twice changes nothing. Kept in memory only: apply it again on boot. A `WorldRayHit.levelObject` is a definition already: spread it and add what to do, as in `LevelEdit.apply({ ...hit.levelObject, model: stump })`.",
                "The edit. Throws for a definition no client could find the object by, or a `model` no client could load."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("LevelEdit[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every edit the server currently has.", "One handle per live edit, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("LevelEdit | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks an edit up by its network entity ID.", "The edit, or null when no live edit has that ID."));
        cls.static_function("restoreAll", &JS_RestoreAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Drops edits, giving every player the objects back as the level had them.", "How many edits were dropped."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
