#include "light_scripting.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "light_service.h"
#include "shared/pose_math.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/light_options.h"

#include <scripting/v8pp_types.h>
#include <stdexcept>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    namespace {
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            try {
                const auto arguments = ReadLightSpawn(info);
                if (!arguments) {
                    return;
                }
                auto *light = Server::Get()->GetLights().Spawn(arguments->position, arguments->tuning);
                if (light == nullptr) {
                    throw std::runtime_error("Light.spawn: failed to create entity");
                }
                info.GetReturnValue().Set(WrapLight(isolate, light->GetNetworkID()));
            }
            catch (const std::exception &error) {
                isolate->ThrowError(v8pp::to_v8(isolate, error.what()));
            }
        }

        template <typename Apply>
        void Retune(Shared::Entities::LightEntity *light, Apply apply) {
            if (light == nullptr) {
                return;
            }
            auto tuning = light->tuning;
            apply(tuning);
            if (!KCDC::Features::Light::LightService::SetTuning(light, tuning)) {
                throw std::invalid_argument("Light: invalid tuning (color 0..1, intensity 0..100, radius 0.1..100)");
            }
        }
    } // namespace

    Shared::Entities::LightEntity *Light::ResolveLight() const {
        return dynamic_cast<Shared::Entities::LightEntity *>(Resolve());
    }

    glm::vec3 Light::GetColor() const {
        const auto *light = ResolveLight();
        return light != nullptr ? light->tuning.color : glm::vec3(0.0F);
    }

    void Light::SetColor(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        const auto color = ReadLightVector(isolate, value, "Light.color");
        if (!color) {
            return;
        }
        Retune(ResolveLight(), [&color](Shared::Game::LightTuning &tuning) {
            tuning.color = *color;
        });
    }

    float Light::GetIntensity() const {
        const auto *light = ResolveLight();
        return light != nullptr ? light->tuning.intensity : 0.0F;
    }

    void Light::SetIntensity(float intensity) {
        Retune(ResolveLight(), [intensity](Shared::Game::LightTuning &tuning) {
            tuning.intensity = intensity;
        });
    }

    float Light::GetRadius() const {
        const auto *light = ResolveLight();
        return light != nullptr ? light->tuning.radius : 0.0F;
    }

    void Light::SetRadius(float radius) {
        Retune(ResolveLight(), [radius](Shared::Game::LightTuning &tuning) {
            tuning.radius = radius;
        });
    }

    bool Light::GetCastShadows() const {
        const auto *light = ResolveLight();
        return light != nullptr && light->tuning.castShadows;
    }

    void Light::SetCastShadows(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (!value->IsBoolean()) {
            isolate->ThrowError("Light.castShadows must be a boolean");
            return;
        }
        Retune(ResolveLight(), [enabled = value->IsTrue()](Shared::Game::LightTuning &tuning) {
            tuning.castShadows = enabled;
        });
    }

    void Light::SetPosition(const Framework::Scripting::Builtins::Vector3 &position) {
        if (!Shared::Game::IsWorldPosition(glm::vec3(position.getX(), position.getY(), position.getZ()))) {
            throw std::invalid_argument("Light.position: expected a finite world-space position");
        }
        Framework::Scripting::Builtins::Entity::SetPosition(position);
    }

    void Light::Destroy() {
        Server::Get()->GetLights().Destroy(ResolveLight());
    }

    v8pp::class_<Light> &Light::GetClass(v8::Isolate *isolate) {
        auto &binding = _classes[isolate];
        if (!binding) {
            Framework::Scripting::Builtins::Entity::GetClass(isolate);
            binding = std::make_unique<v8pp::class_<Light>>(isolate, ServerCatalog(), "Light", "Replicated point light that illuminates its surroundings.");
            binding->auto_wrap_objects(true);
            binding->inherit<Framework::Scripting::Builtins::Entity>().ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing entity."));
            binding->property("position", &Light::GetPosition, &Light::SetPosition, v8pp::metadata::property_docs("Vector3", "World-space position in metres. Invalid positions throw before mutation."));
            binding->function("destroy", &Light::Destroy, v8pp::metadata::docs("void", {}, "Removes this light from every client. Safe to repeat."));
            binding->property("color", &Light::GetColor, &Light::SetColor, v8pp::metadata::property_docs("Vector3", "RGB color using x, y and z, each from 0 to 1. Invalid values throw before mutation."));
            binding->property("intensity", &Light::GetIntensity, &Light::SetIntensity, v8pp::metadata::property_docs("number", "Diffuse intensity multiplier, from 0 to 100. Invalid values throw before mutation."));
            binding->property("radius", &Light::GetRadius, &Light::SetRadius, v8pp::metadata::property_docs("number", "Light radius in metres, from 0.1 to 100. Invalid values throw before mutation."));
            binding->property("castShadows", &Light::GetCastShadows, &Light::SetCastShadows, v8pp::metadata::property_docs("boolean", "Whether this light casts native shadows. False by default; only booleans are accepted."));
        }
        return *binding;
    }

    void Light::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &binding = GetClass(isolate);
        binding.static_function("spawn", JS_Spawn,
            v8pp::metadata::docs("Light",
                {v8pp::metadata::param("position", "Vector3 | { x: number; y: number; z: number }", false, "World-space position in metres."),
                    v8pp::metadata::param("options", "{ color?: Vector3 | { x: number; y: number; z: number }; intensity?: number; radius?: number; castShadows?: boolean }", true,
                        "RGB color 0..1, intensity 0..100 and radius 0.1..100 metres. Defaults to warm orange, intensity 1, radius 5 and castShadows false. Set castShadows to true for native shadows.")},
                "Creates a persistent point light, streamed to nearby players including late arrivals. Removed when its creating script resource stops. Exported world lights are owned by the server.", "The new light handle."));
        binding.publish(global);
    }

    void Light::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace KCDC::Scripting
