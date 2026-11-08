#include "world_resource_scripting.h"
#include "features/patrol/patrol_scripting.h"

#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/area/area_scripting.h"
#include "features/level_edit/level_edit_scripting.h"
#include "features/smart_object/smart_object_scripting.h"
#include "shared/scripting/catalog.h"
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {

    namespace {
        KCDC::Features::WorldBuilder::WorldBuilderService &Resources() {
            return Server::Get()->GetWorldResources();
        }

        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                                                            = info.GetIsolate();
            const std::vector<KCDC::Features::WorldBuilder::WorldResourceRecord> &resources = Resources().All();
            v8::Local<v8::Array> result                                                     = v8::Array::New(isolate, static_cast<int>(resources.size()));
            for (std::size_t index = 0; index < resources.size(); ++index) {
                result->Set(isolate->GetCurrentContext(), static_cast<std::uint32_t>(index), v8pp::class_<WorldResource>::create_object(isolate, resources[index].handle)).Check();
            }
            info.GetReturnValue().Set(result);
        }

        void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() != 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "WorldResource.find: expected (name: string)"));
                return;
            }
            const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resources().Find(v8pp::from_v8<std::string>(isolate, info[0]));
            if (resource != nullptr) {
                info.GetReturnValue().Set(v8pp::class_<WorldResource>::create_object(isolate, resource->handle));
            }
            else {
                info.GetReturnValue().SetNull();
            }
        }

        void JS_Ready(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(Resources().IsReady());
        }

        template <typename Id, typename Wrap>
        v8::Local<v8::Value> LiveMembers(v8::Isolate *isolate, const std::vector<Id> *ids, Wrap wrap) {
            v8::Local<v8::Array> result = v8::Array::New(isolate);
            if (ids == nullptr) {
                return result;
            }
            // Members a script destroyed resolve to null and drop out.
            std::uint32_t index = 0;
            for (const Id id : *ids) {
                const v8::Local<v8::Value> value = wrap(isolate, id);
                if (!value->IsNull()) {
                    result->Set(isolate->GetCurrentContext(), index++, value).Check();
                }
            }
            return result;
        }
    } // namespace

    const KCDC::Features::WorldBuilder::WorldResourceRecord *WorldResource::Resolve() const {
        return Resources().Get(_handle);
    }

    std::string WorldResource::GetName() const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return resource != nullptr ? resource->name : std::string();
    }

    std::string WorldResource::GetPath() const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return resource != nullptr ? resource->path : std::string();
    }

    std::string WorldResource::GetLevel() const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return resource != nullptr ? resource->level : std::string();
    }

    bool WorldResource::IsLoaded() const {
        return Resolve() != nullptr;
    }

    v8::Local<v8::Value> WorldResource::GetProps(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->props : nullptr, WrapProp);
    }

    v8::Local<v8::Value> WorldResource::GetLights(v8::Isolate *isolate) const {
        const auto *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->lights : nullptr, WrapLight);
    }

    v8::Local<v8::Value> WorldResource::GetEffects(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->effects : nullptr, WrapVfx);
    }

    v8::Local<v8::Value> WorldResource::GetLevelEdits(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->levelEdits : nullptr, WrapLevelEdit);
    }

    v8::Local<v8::Value> WorldResource::GetDoors(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->doors : nullptr, WrapDoor);
    }

    v8::Local<v8::Value> WorldResource::GetSmartObjects(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->smartObjects : nullptr, WrapSmartObject);
    }

    v8::Local<v8::Value> WorldResource::GetItems(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->items : nullptr, WrapGroundItem);
    }

    v8::Local<v8::Value> WorldResource::GetAreas(v8::Isolate *isolate) const {
        const KCDC::Features::WorldBuilder::WorldResourceRecord *resource = Resolve();
        return LiveMembers(isolate, resource != nullptr ? &resource->areas : nullptr, WrapArea);
    }

    v8::Local<v8::Value> WorldResource::GetPatrolRoutes(v8::Isolate *isolate) const {
        const auto *resource = Resolve();
        return LiveMembers(isolate, resource ? &resource->patrolRoutes : nullptr, WrapPatrolRoute);
    }

    void WorldResource::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &_class = _classes[isolate];
        ServerCatalog()
            .data_type("EventMap")
            .add_property("worldResourcesReady", "[]",
                "Raised once after every configured world export has loaded successfully, including an empty list. WorldResource.ready is true inside the handler. A script starting later must check WorldResource.ready first; the event is not replayed. Do not await this event inside resourceStart: exports load after script startup. Handler promises are not awaited.");
        _class = std::make_unique<v8pp::class_<WorldResource>>(isolate, ServerCatalog(), "WorldResource",
            "A named world export loaded from mod.world_resources. Server-owned, with live access to its surviving props, effects, level edits, doors, smart objects, items, areas and patrol routes. Obtain handles through all or find; runtime loading, reloading and unloading are not exposed.");
        _class->auto_wrap_objects(true);
        _class->property("name", &WorldResource::GetName, v8pp::metadata::property_docs("string", "Unique case-sensitive resource name: the filename without .world.json, or the explicit config name. Empty after shutdown."));
        _class->property("path", &WorldResource::GetPath, v8pp::metadata::property_docs("string", "Resolved absolute source file path, in UTF-8. Empty after shutdown."));
        _class->property("level", &WorldResource::GetLevel, v8pp::metadata::property_docs("string", "The exported game level, checked against the server level at startup. Empty after shutdown."));
        _class->property("loaded", &WorldResource::IsLoaded, v8pp::metadata::property_docs("boolean", "Whether this export is registered. Remains true even if scripts destroy all of its contents."));
        _class->property("props", &WorldResource::GetProps, v8pp::metadata::property_docs("Prop[]", "A fresh array of surviving props in export order. Changing the array does not change membership; the handles support normal Prop operations."));
        _class->property("lights", &WorldResource::GetLights, v8pp::metadata::property_docs("Light[]", "A fresh array of surviving point lights in export order, excluding destroyed lights."));
        _class->property("effects", &WorldResource::GetEffects, v8pp::metadata::property_docs("Vfx[]", "A fresh array of surviving effects in export order, excluding destroyed effects."));
        _class->property("levelEdits", &WorldResource::GetLevelEdits, v8pp::metadata::property_docs("LevelEdit[]", "A fresh array of surviving level edits in export order, excluding restored edits."));
        _class->property("doors", &WorldResource::GetDoors,
            v8pp::metadata::property_docs("Door[]", "A fresh array of the surviving doors the export hangs, in export order, excluding destroyed ones. A door the export takes out of the level is a level edit, in `levelEdits`; `Door.removed` lists those."));
        _class->property("smartObjects", &WorldResource::GetSmartObjects,
            v8pp::metadata::property_docs("SmartObject[]", "A fresh array of the surviving seats, beds, tables and stations the export places, in export order, excluding destroyed ones. A level station the export takes out is a level edit, in `levelEdits`."));
        _class->property("items", &WorldResource::GetItems, v8pp::metadata::property_docs("GroundItem[]", "A fresh array of the stacks the export lays on the ground that nobody has picked up yet, in export order. Each one's `name` is the name the export gave it."));
        _class->property("areas", &WorldResource::GetAreas, v8pp::metadata::property_docs("Area[]", "A fresh array of surviving exported areas in export order, excluding destroyed areas."));
        _class->property("patrolRoutes", &WorldResource::GetPatrolRoutes, v8pp::metadata::property_docs("PatrolRoute[]", "Surviving exported patrol definitions, in export order."));
        auto ready     = v8pp::metadata::property_docs("boolean", "True after every configured export has loaded successfully, even when the config list is empty. Check this before subscribing to worldResourcesReady when a script may start later.");
        ready.readonly = true;
        ready.static_  = true;
        _class->document_property("ready", std::move(ready));
        _class->js_function_template()->SetAccessorProperty(v8pp::to_v8(isolate, "ready"), v8::FunctionTemplate::New(isolate, JS_Ready), {}, v8::DontDelete);
        _class->static_function("all", JS_All, v8pp::metadata::docs("WorldResource[]", {}, "Lists config-loaded world exports in configuration order. Empty before successful startup and after shutdown.", "A fresh array of resource handles."));
        _class->static_function("find", JS_Find,
            v8pp::metadata::docs("WorldResource | null", {v8pp::metadata::param("name", "string", false, "Exact, case-sensitive resource name.")}, "Finds a config-loaded export by name. Throws for a non-string argument.",
                "The resource, or null when absent or startup has not completed."));
        _class->publish(global);
    }

    void WorldResource::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace KCDC::Scripting
