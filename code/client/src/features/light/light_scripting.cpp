#include "light_scripting.h"
#include "core/application.h"
#include "core/scripting/script_context.h"
#include "light_service.h"
#include "shared/pose_math.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/light_options.h"

#include <scripting/v8pp_types.h>
#include <stdexcept>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    namespace {
        Features::Light::LightService &Service() {
            return Core::gApplication->GetLightService();
        }

        float ReadNumber(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *name) {
            if (!value->IsNumber()) {
                throw std::invalid_argument(std::string(name) + ": expected a number");
            }
            return static_cast<float>(value->NumberValue(isolate->GetCurrentContext()).FromMaybe(0));
        }

        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            try {
                const auto arguments = ReadLightSpawn(info);
                if (!arguments) {
                    return;
                }
                const std::string owner = KCDC::ResolveCallingResource(isolate);
                if (owner.empty()) {
                    throw std::runtime_error("Light.spawn: the calling resource could not be identified");
                }
                Service().AttachResources(KCDC::ScriptResources());
                const auto handle = Service().Spawn(arguments->position, arguments->tuning, owner);
                if (handle == Features::Light::LightService::kNoHandle) {
                    throw std::runtime_error("Light.spawn: world unavailable, light budget exhausted or native creation failed");
                }
                info.GetReturnValue().Set(v8pp::class_<Light>::create_object(isolate, handle));
            }
            catch (const std::exception &error) {
                isolate->ThrowError(v8pp::to_v8(isolate, error.what()));
            }
        }

        template <typename Apply>
        void Retune(std::uint32_t id, Apply apply) {
            glm::vec3 position;
            Shared::Game::LightTuning tuning;
            if (!Service().Describe(id, position, tuning)) {
                return;
            }
            apply(tuning);
            if (!tuning.IsSane()) {
                throw std::invalid_argument("Light: invalid tuning (color 0..1, intensity 0..100, radius 0.1..100)");
            }
            if (!Service().SetTuning(id, tuning)) {
                throw std::runtime_error("Light: native update failed");
            }
        }

        Shared::Game::LightTuning Tuning(std::uint32_t id) {
            glm::vec3 position;
            Shared::Game::LightTuning tuning;
            if (!Service().Describe(id, position, tuning)) {
                tuning.color       = glm::vec3(0);
                tuning.intensity   = 0;
                tuning.radius      = 0;
                tuning.castShadows = false;
            }
            return tuning;
        }
    } // namespace

    bool Light::IsValid() const {
        return Service().IsAlive(_id);
    }
    glm::vec3 Light::GetPosition() const {
        glm::vec3 position(0);
        Shared::Game::LightTuning tuning;
        Service().Describe(_id, position, tuning);
        return position;
    }
    void Light::SetPosition(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        const auto position = ReadLightVector(isolate, value, "Light.position");
        if (!position) {
            return;
        }
        if (!Shared::Game::IsWorldPosition(*position)) {
            throw std::invalid_argument("Light.position: expected a finite world-space position");
        }
        if (IsValid() && !Service().Move(_id, *position)) {
            throw std::runtime_error("Light.position: native update failed");
        }
    }
    glm::vec3 Light::GetColor() const {
        return Tuning(_id).color;
    }
    void Light::SetColor(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        const auto color = ReadLightVector(isolate, value, "Light.color");
        if (!color) {
            return;
        }
        Retune(_id, [&color](auto &tuning) {
            tuning.color = *color;
        });
    }
    float Light::GetIntensity() const {
        return Tuning(_id).intensity;
    }
    void Light::SetIntensity(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        const float intensity = ReadNumber(isolate, value, "Light.intensity");
        Retune(_id, [intensity](auto &tuning) {
            tuning.intensity = intensity;
        });
    }
    float Light::GetRadius() const {
        return Tuning(_id).radius;
    }
    void Light::SetRadius(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        const float radius = ReadNumber(isolate, value, "Light.radius");
        Retune(_id, [radius](auto &tuning) {
            tuning.radius = radius;
        });
    }
    bool Light::GetCastShadows() const {
        return Tuning(_id).castShadows;
    }
    void Light::SetCastShadows(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsBoolean()) {
            isolate->ThrowError("Light.castShadows: expected a boolean");
            return;
        }
        Retune(_id, [enabled = value->IsTrue()](auto &tuning) {
            tuning.castShadows = enabled;
        });
    }
    void Light::Destroy() {
        Service().Stop(_id);
    }

    v8pp::class_<Light> &Light::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<Light>>(isolate, ClientCatalog(), "Light", "A point light created on this client. Not replicated; server scripts can use events to coordinate local lights, or the server Light API for persistent replicated lights.");
            _class->auto_wrap_objects(true);
            _class->property("id", &Light::GetId, v8pp::metadata::property_docs("number", "Client-local service handle, distinct from server network entity identifiers."));
            _class->property("valid", &Light::IsValid, v8pp::metadata::property_docs("boolean", "Whether the native light still exists. False after destruction, resource cleanup or a world change."));
            _class->property("position", &Light::GetPosition, &Light::SetPosition, v8pp::metadata::property_docs("Vector3", "World-space position in metres. Invalid positions throw before mutation."));
            _class->property("color", &Light::GetColor, &Light::SetColor, v8pp::metadata::property_docs("Vector3", "RGB color using x, y and z, each from 0 to 1."));
            _class->property("intensity", &Light::GetIntensity, &Light::SetIntensity, v8pp::metadata::property_docs("number", "Diffuse intensity multiplier, from 0 to 100."));
            _class->property("radius", &Light::GetRadius, &Light::SetRadius, v8pp::metadata::property_docs("number", "Light radius in metres, from 0.1 to 100."));
            _class->property("castShadows", &Light::GetCastShadows, &Light::SetCastShadows, v8pp::metadata::property_docs("boolean", "Whether to cast native shadows. False by default; rendering also depends on the game's shadow settings."));
            _class->function("destroy", &Light::Destroy, v8pp::metadata::docs("void", {}, "Removes this client-local light. Safe to repeat."));
            _class->static_function("spawn", JS_Spawn,
                v8pp::metadata::docs("Light",
                    {v8pp::metadata::param("position", "Vector3 | { x: number; y: number; z: number }", false, "World-space position in metres."),
                        v8pp::metadata::param("options", "{ color?: Vector3 | { x: number; y: number; z: number }; intensity?: number; radius?: number; castShadows?: boolean }", true, "Defaults to warm orange, intensity 1, radius 5 and shadows off.")},
                    "Creates a client-local point light until destroyed, its creating resource stops or errors, or the session/world ends. Throws when the world is unavailable, native creation fails or the shared 192-light budget is full.", "The new local light handle."));
        }
        return *_class;
    }

    void Light::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }
        if (Core::gApplication) {
            Service().AttachResources(KCDC::ScriptResources());
        }
        GetClass(isolate).publish(global);
    }
} // namespace KCDC::Scripting
