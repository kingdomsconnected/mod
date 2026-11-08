#pragma once

#include <scripting/builtins/quaternion.h>
#include <scripting/v8pp_types.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace KCDC::Scripting {
    // Reads a rotation argument the way the Entity base's setter does: a Quaternion as it is, anything else with x/y/z as Euler angles in degrees. Every spawn verb takes one, so they all take it the same way.
    inline glm::quat ParseRotationArg(v8::Isolate *isolate, v8::Local<v8::Value> value, const glm::quat &fallback) {
        if (value.IsEmpty() || !value->IsObject()) {
            return fallback;
        }
        if (auto *quat = v8pp::class_<Framework::Scripting::Builtins::Quaternion>::unwrap_object(isolate, value)) {
            const glm::quat parsed = quat->quat();
            return glm::dot(parsed, parsed) > 0.0F ? glm::normalize(parsed) : fallback;
        }

        // The glm converters in <scripting/v8pp_types.h> are convert<> specialisations; the generic from_v8<T> would try to unwrap a registered class_<glm::vec3> first and throw, so call them directly.
        return glm::quat(glm::radians(v8pp::convert<glm::vec3>::from_v8(isolate, value)));
    }
} // namespace KCDC::Scripting
