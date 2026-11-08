#pragma once

#include "shared/features/vfx/vfx_tuning.h"
#include "shared/scripting/option_field.h"
#include "shared/scripting/rotation_arg.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <scripting/v8pp_types.h>
#include <v8.h>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    /**
     * The options bag every `Vfx` verb takes, on both peers.
     *
     * Shared because the client's `Vfx` and the server's are the same verbs
     * pointed at different machines, and an option that meant one thing in a
     * client resource and another in a server one would be a trap. Absent
     * fields keep their defaults; a present field of the wrong type is ignored
     * rather than refused, like the rest of the API's option bags.
     */
    struct VfxOptions {
        Shared::Game::VfxTuning tuning;
        glm::vec3 offset {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        std::uint32_t durationMs = 0;
        bool durationGiven       = false;
    };

    /**
     * Reads the options object at `index`, or leaves the defaults when there is
     * none. Numeric knobs are clamped rather than refused: a resource asking
     * for twice the legal scale wants a bigger effect, and the useful answer is
     * the biggest one allowed.
     */
    inline VfxOptions ParseVfxOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        VfxOptions parsed;
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= index || !info[index]->IsObject()) {
            return parsed;
        }

        v8::Local<v8::Object> options = info[index].As<v8::Object>();
        float value                   = 0.0F;

        if (FloatOption(isolate, options, "scale", value)) {
            parsed.tuning.scale = std::clamp(value, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxScale);
        }
        if (FloatOption(isolate, options, "countScale", value)) {
            parsed.tuning.countScale = std::clamp(value, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxCountScale);
        }
        if (FloatOption(isolate, options, "speedScale", value)) {
            parsed.tuning.speedScale = std::clamp(value, 0.0F, Shared::Game::VfxTuning::kMaxSpeedScale);
        }
        if (FloatOption(isolate, options, "timeScale", value)) {
            parsed.tuning.timeScale = std::clamp(value, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxTimeScale);
        }
        if (FloatOption(isolate, options, "strength", value)) {
            parsed.tuning.strength = std::clamp(value, -1.0F, 1.0F);
        }
        if (FloatOption(isolate, options, "pulsePeriod", value)) {
            parsed.tuning.pulsePeriod = std::clamp(value, 0.0F, Shared::Game::VfxTuning::kMaxPulsePeriod);
        }
        if (FloatOption(isolate, options, "durationMs", value)) {
            parsed.durationMs    = static_cast<std::uint32_t>(std::clamp(value, 0.0F, 3600000.0F));
            parsed.durationGiven = true;
        }

        const v8::Local<v8::Value> prime = OptionField(isolate, options, "prime");
        if (!prime.IsEmpty() && prime->IsBoolean()) {
            parsed.tuning.prime = v8pp::from_v8<bool>(isolate, prime);
        }

        const v8::Local<v8::Value> offset = OptionField(isolate, options, "offset");
        if (!offset.IsEmpty() && offset->IsObject()) {
            parsed.offset = v8pp::convert<glm::vec3>::from_v8(isolate, offset);
        }

        const v8::Local<v8::Value> rotation = OptionField(isolate, options, "rotation");
        if (!rotation.IsEmpty()) {
            parsed.rotation = ParseRotationArg(isolate, rotation, parsed.rotation);
        }
        return parsed;
    }

    /** The TypeScript the options bag is documented as, so both bindings spell it identically. */
    inline constexpr const char *kVfxOptionsType = "{ rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }";

    /** The same bag, plus the offset the attaching verbs take. */
    inline constexpr const char *kVfxAttachOptionsType = "{ offset?: Vector3; rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }";
} // namespace KCDC::Scripting
