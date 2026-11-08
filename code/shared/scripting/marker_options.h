#pragma once

#include "shared/features/marker/marker_entity.h"
#include "shared/features/marker/marker_shape.h"
#include "shared/scripting/option_field.h"
#include "shared/scripting/rotation_arg.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <scripting/v8pp_types.h>
#include <string>
#include <v8.h>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    /** The options bag every `Marker` verb takes. Absent or mistyped fields keep their defaults, like the rest of the API's option bags. */
    struct MarkerOptions {
        Shared::Entities::MarkerShape shape = Shared::Entities::MarkerShape::Decal;
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        float size   = 4.0F;
        float height = 1.0F;
        float spin          = 0.0F;
        float bob           = 0.0F;
        std::uint32_t color = 0;
        bool trigger        = false;
    };

    /** Reads the options object at `index`. Numeric knobs are clamped rather than refused: a resource asking for a hundred-metre zone wants the biggest one it can have. */
    inline MarkerOptions ParseMarkerOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        MarkerOptions parsed;
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= index || !info[index]->IsObject()) {
            return parsed;
        }

        v8::Local<v8::Object> options = info[index].As<v8::Object>();
        float value                   = 0.0F;

        if (FloatOption(isolate, options, "size", value)) {
            parsed.size = std::clamp(value, Shared::Entities::MarkerEntity::kMinSize, Shared::Entities::MarkerEntity::kMaxSize);
        }
        if (FloatOption(isolate, options, "height", value)) {
            parsed.height = std::clamp(value, Shared::Entities::MarkerEntity::kMinHeight, Shared::Entities::MarkerEntity::kMaxHeight);
        }
        if (FloatOption(isolate, options, "spin", value)) {
            parsed.spin = std::clamp(value, -Shared::Entities::MarkerEntity::kMaxSpin, Shared::Entities::MarkerEntity::kMaxSpin);
        }
        if (FloatOption(isolate, options, "bob", value)) {
            parsed.bob = std::clamp(value, 0.0F, Shared::Entities::MarkerEntity::kMaxBob);
        }
        if (FloatOption(isolate, options, "color", value)) {
            parsed.color = static_cast<std::uint32_t>(std::clamp(value, 0.0F, 4294967295.0F));
        }

        const v8::Local<v8::Value> trigger = OptionField(isolate, options, "trigger");
        if (!trigger.IsEmpty() && trigger->IsBoolean()) {
            parsed.trigger = v8pp::from_v8<bool>(isolate, trigger);
        }

        const v8::Local<v8::Value> shape = OptionField(isolate, options, "shape");
        if (!shape.IsEmpty() && shape->IsString()) {
            parsed.shape = Shared::Entities::MarkerShapeFromName(v8pp::from_v8<std::string>(isolate, shape));
        }

        const v8::Local<v8::Value> rotation = OptionField(isolate, options, "rotation");
        if (!rotation.IsEmpty()) {
            parsed.rotation = ParseRotationArg(isolate, rotation, parsed.rotation);
        }
        return parsed;
    }

    /** The TypeScript the options bag is documented as, so every binding spells it identically. */
    inline constexpr const char *kMarkerOptionsType = "{ shape?: 'decal' | 'cylinder' | 'sphere' | 'chevron' | 'cube' | 'plane'; rotation?: Quaternion | Vector3; size?: number; height?: number; spin?: number; bob?: number; color?: number; trigger?: boolean }";
} // namespace KCDC::Scripting
