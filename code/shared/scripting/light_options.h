#pragma once

#include "shared/features/light/light_tuning.h"
#include "shared/pose_math.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <v8.h>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    // Keep light vectors strict on both sides: Vector3 and plain {x,y,z} are
    // accepted, while arrays and coerced numeric strings are not.
    inline std::optional<glm::vec3> ReadLightVector(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *name) {
        if (!value->IsObject() || value->IsArray() || value->IsNull()) {
            throw std::invalid_argument(std::string(name) + ": expected numeric x, y and z components");
        }
        const auto object = value.As<v8::Object>();
        glm::vec3 result;
        const char *keys[] = {"x", "y", "z"};
        for (int index = 0; index < 3; ++index) {
            v8::Local<v8::Value> component;
            if (!object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, keys[index])).ToLocal(&component)) {
                return std::nullopt; // Preserve the getter's pending exception.
            }
            if (!component->IsNumber()) {
                throw std::invalid_argument(std::string(name) + ": expected numeric x, y and z components");
            }
            result[index] = static_cast<float>(component->NumberValue(isolate->GetCurrentContext()).FromMaybe(0));
        }
        return result;
    }

    struct LightSpawnArguments {
        glm::vec3 position;
        Shared::Game::LightTuning tuning;
    };

    inline std::optional<LightSpawnArguments> ReadLightSpawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() < 1) {
            throw std::invalid_argument("Light.spawn: expected (position, options?)");
        }
        auto *isolate = info.GetIsolate();
        auto position = ReadLightVector(isolate, info[0], "Light.spawn position");
        if (!position) {
            return std::nullopt;
        }
        Shared::Game::LightTuning tuning;
        if (info.Length() >= 2 && !info[1]->IsUndefined()) {
            if (!info[1]->IsObject() || info[1]->IsArray() || info[1]->IsNull()) {
                throw std::invalid_argument("Light.spawn: options must be an object");
            }
            const auto options = info[1].As<v8::Object>();
            for (const char *key : {"color", "intensity", "radius", "castShadows"}) {
                v8::Local<v8::Value> value;
                if (!options->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value)) {
                    return std::nullopt; // Preserve the getter's pending exception.
                }
                if (value->IsUndefined()) {
                    continue;
                }
                if (std::string_view(key) == "color") {
                    auto color = ReadLightVector(isolate, value, "Light.spawn color");
                    if (!color) {
                        return std::nullopt;
                    }
                    tuning.color = *color;
                }
                else if (std::string_view(key) == "castShadows") {
                    if (!value->IsBoolean()) {
                        throw std::invalid_argument("Light.spawn: castShadows must be a boolean");
                    }
                    tuning.castShadows = value->IsTrue();
                }
                else {
                    if (!value->IsNumber()) {
                        throw std::invalid_argument(std::string("Light.spawn: ") + key + " must be a number");
                    }
                    const float number                                                        = static_cast<float>(value->NumberValue(isolate->GetCurrentContext()).FromMaybe(0));
                    (std::string_view(key) == "intensity" ? tuning.intensity : tuning.radius) = number;
                }
            }
        }
        if (!Shared::Game::IsWorldPosition(*position) || !tuning.IsSane()) {
            throw std::invalid_argument("Light.spawn: invalid position or tuning (color 0..1, intensity 0..100, radius 0.1..100)");
        }
        return LightSpawnArguments {*position, tuning};
    }
} // namespace KCDC::Scripting
