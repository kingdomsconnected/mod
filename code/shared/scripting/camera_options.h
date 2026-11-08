#pragma once

#include "shared/features/camera/camera_rpc.h"
#include "shared/pose_math.h"
#include "shared/scripting/option_field.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <scripting/v8pp_types.h>
#include <v8.h>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    namespace CameraOptionDetail {
        /** A finite Vector3 off an options bag, or false when it is missing or unreadable. */
        inline bool PointOption(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, glm::vec3 &point) {
            const v8::Local<v8::Value> value = OptionField(isolate, options, key);
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }
            const glm::vec3 parsed = v8pp::convert<glm::vec3>::from_v8(isolate, value);
            if (!Shared::Game::IsFinite(parsed)) {
                return false;
            }
            point = parsed;
            return true;
        }
    } // namespace CameraOptionDetail

    /**
     * The options bag `Camera.setShot` takes, on both peers.
     *
     * Shared because the client's `setShot` and the server's are the same verb
     * pointed at different machines: a `duration` one of them clamped
     * differently, or an `fov` one read and the other ignored, would be a
     * trap. `position` and `lookAt` are the two the verb cannot invent, so it
     * refuses without them; the rest keep their defaults.
     */
    struct CameraShotOptions {
        glm::vec3 position {0.0F};
        glm::vec3 lookAt {0.0F};

        /** 0 is a cut; otherwise the glide from wherever the view is now. */
        std::uint32_t durationMs = 0;

        /** Vertical degrees inside the shot's range, or 0 for the player's own lens. */
        float fieldOfView = 0.0F;
    };

    /** Milliseconds a glide may take, clamped to the shot's ceiling; anything unreadable is a cut. */
    inline std::uint32_t ClampGlideMilliseconds(double milliseconds) {
        if (!std::isfinite(milliseconds)) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::clamp(milliseconds, 0.0, static_cast<double>(Shared::RPC::CameraShot::kMaxDurationMs)));
    }

    /** The glide duration argument at `index`, or a cut when it is absent or not a number. */
    inline std::uint32_t GlideDurationArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        if (info.Length() <= index || !info[index]->IsNumber()) {
            return 0;
        }
        return ClampGlideMilliseconds(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
    }

    /**
     * Reads the shot options object at `index`. False when it is missing, is
     * not an object, or carries no finite `position` and `lookAt`.
     */
    inline bool ParseCameraShotOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index, CameraShotOptions &parsed) {
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= index || !info[index]->IsObject()) {
            return false;
        }

        v8::Local<v8::Object> options = info[index].As<v8::Object>();
        if (!CameraOptionDetail::PointOption(isolate, options, "position", parsed.position) || !CameraOptionDetail::PointOption(isolate, options, "lookAt", parsed.lookAt)) {
            return false;
        }

        float value = 0.0F;
        if (FloatOption(isolate, options, "duration", value)) {
            parsed.durationMs = ClampGlideMilliseconds(value);
        }

        // A lens is clamped rather than refused, as the rest of the API's
        // numeric knobs are; 0 and below mean the player's own.
        if (FloatOption(isolate, options, "fov", value) && value > 0.0F) {
            parsed.fieldOfView = std::clamp(value, Shared::RPC::CameraShot::kMinFieldOfView, Shared::RPC::CameraShot::kMaxFieldOfView);
        }
        return true;
    }

    /** The TypeScript the shot's options bag is documented as, so both bindings spell it identically. */
    inline constexpr const char *kCameraShotOptionsType = "{ position: Vector3; lookAt: Vector3; duration?: number; fov?: number }";
} // namespace KCDC::Scripting
