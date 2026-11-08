#include "features/camera/noclip_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"

#include "features/camera/anchor_camera.h"
#include "features/camera/camera_control_service.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** How far back `focus` stands when the caller does not say. */
        constexpr float kDefaultFocusDistance = 6.0F;

        KCDC::Features::Camera::CameraControlService *Service() {
            return Core::gApplication ? &Core::gApplication->GetCameraControl() : nullptr;
        }

        /** The service, but only while it is actually flying. */
        KCDC::Features::Camera::CameraControlService *ActiveService() {
            KCDC::Features::Camera::CameraControlService *service = Service();
            return service != nullptr && service->IsActive() ? service : nullptr;
        }

        /** A finite world-space point in that slot, or false. */
        bool ReadPoint(const v8::FunctionCallbackInfo<v8::Value> &info, int index, SDK::Vec3 &point) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return false;
            }
            const glm::vec3 value = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
            if (!KCDC::Common::IsFinite(value)) {
                return false;
            }
            point = KCDC::Common::ToNative(value);
            return true;
        }

        v8::Local<v8::Value> VectorToV8(v8::Isolate *isolate, const SDK::Vec3 &value) {
            return v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(value));
        }

        /** The two angles as `{ yaw, pitch }`, which is what the camera has. */
        v8::Local<v8::Value> AnglesToV8(v8::Isolate *isolate, const KCDC::Features::Camera::CameraPose &pose) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "yaw"), v8pp::to_v8(isolate, pose.yawDegrees)).Check();
            result->Set(context, v8pp::to_v8(isolate, "pitch"), v8pp::to_v8(isolate, pose.pitchDegrees)).Check();
            return result;
        }
    } // namespace

    void NoClip::JS_Enable(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Camera::NoClipOptions options {};

        const std::string mode = StringOption(isolate, info, 0, "mode");
        if (!mode.empty() && !KCDC::Features::Camera::ParseBodyMode(mode, options.body)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.enable: `" + mode + "` is not one of `body` or `camera`"));
            return;
        }

        options.builtInInput = BoolOption(isolate, info, 0, "input", options.builtInInput);
        options.speed        = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "speed", options.speed), 0.05, 400.0));
        options.fieldOfView  = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "fov", 0.0), 0.0, 140.0));

        KCDC::Features::Camera::CameraControlService *service    = Service();
        const KCDC::Features::Camera::EnableResult result = service != nullptr ? service->Enable(options) : KCDC::Features::Camera::EnableResult::NoCamera;
        info.GetReturnValue().Set(v8pp::to_v8(isolate, KCDC::Features::Camera::EnableResultName(result)));
    }

    void NoClip::JS_Disable(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        const bool keepPosition = BoolOption(isolate, info, 0, "keepPosition", true);
        if (KCDC::Features::Camera::CameraControlService *service = Service()) {
            service->Disable(keepPosition, KCDC::Features::Camera::EndReason::Script);
        }
    }

    void NoClip::JS_IsActive(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(ActiveService() != nullptr);
    }

    void NoClip::JS_GetState(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }

        const KCDC::Features::Camera::CameraPose &pose = service->GetCamera().GetPose();
        v8::Local<v8::Context> context                 = isolate->GetCurrentContext();
        v8::Local<v8::Object> result                   = v8::Object::New(isolate);
        result->Set(context, v8pp::to_v8(isolate, "mode"), v8pp::to_v8(isolate, KCDC::Features::Camera::BodyModeName(service->GetBodyMode()))).Check();
        result->Set(context, v8pp::to_v8(isolate, "input"), v8pp::to_v8(isolate, service->IsBuiltInInputEnabled())).Check();
        result->Set(context, v8pp::to_v8(isolate, "position"), VectorToV8(isolate, pose.position)).Check();
        result->Set(context, v8pp::to_v8(isolate, "forward"), VectorToV8(isolate, KCDC::Features::Camera::PoseForward(pose))).Check();
        result->Set(context, v8pp::to_v8(isolate, "right"), VectorToV8(isolate, KCDC::Features::Camera::PoseRight(pose))).Check();
        result->Set(context, v8pp::to_v8(isolate, "up"), VectorToV8(isolate, KCDC::Features::Camera::PoseUp(pose))).Check();
        result->Set(context, v8pp::to_v8(isolate, "yaw"), v8pp::to_v8(isolate, pose.yawDegrees)).Check();
        result->Set(context, v8pp::to_v8(isolate, "pitch"), v8pp::to_v8(isolate, pose.pitchDegrees)).Check();
        result->Set(context, v8pp::to_v8(isolate, "speed"), v8pp::to_v8(isolate, service->GetCamera().GetSpeed())).Check();
        result->Set(context, v8pp::to_v8(isolate, "fov"), v8pp::to_v8(isolate, service->GetCamera().GetRequestedFieldOfView())).Check();
        result->Set(context, v8pp::to_v8(isolate, "renderedFov"), v8pp::to_v8(isolate, pose.fovDegrees)).Check();
        result->Set(context, v8pp::to_v8(isolate, "aspectRatio"), v8pp::to_v8(isolate, pose.aspectRatio)).Check();
        info.GetReturnValue().Set(result);
    }

    void NoClip::JS_GetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(VectorToV8(isolate, service->GetCamera().GetPose().position));
    }

    void NoClip::JS_SetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 position {};
        if (!ReadPoint(info, 0, position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setPosition: expected (position)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }
        service->GetCamera().SetPosition(position);
        info.GetReturnValue().Set(true);
    }

    void NoClip::JS_GetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(AnglesToV8(isolate, service->GetCamera().GetPose()));
    }

    void NoClip::JS_SetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setRotation: expected ({ yaw?, pitch? })"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }

        const KCDC::Features::Camera::CameraPose &pose = service->GetCamera().GetPose();
        const float yaw                                = static_cast<float>(NumberOption(isolate, info, 0, "yaw", pose.yawDegrees));
        const float pitch                              = static_cast<float>(NumberOption(isolate, info, 0, "pitch", pose.pitchDegrees));
        service->GetCamera().SetRotation(yaw, pitch);
        info.GetReturnValue().Set(true);
    }

    void NoClip::JS_GetForward(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(VectorToV8(isolate, KCDC::Features::Camera::PoseForward(service->GetCamera().GetPose())));
    }

    void NoClip::JS_SetPose(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 position {};
        SDK::Vec3 forward {};
        if (!ReadPoint(info, 0, position) || !ReadPoint(info, 1, forward)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setPose: expected (position, forward)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }
        service->GetCamera().SetPose(position, forward);
        info.GetReturnValue().Set(true);
    }

    void NoClip::JS_LookAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 target {};
        if (!ReadPoint(info, 0, target)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.lookAt: expected (point)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }

        const SDK::Vec3 position = service->GetCamera().GetPose().position;
        service->GetCamera().SetPose(position, SDK::Vec3 {target.x - position.x, target.y - position.y, target.z - position.z});
        info.GetReturnValue().Set(true);
    }

    void NoClip::JS_Focus(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 target {};
        if (!ReadPoint(info, 0, target)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.focus: expected (target, distance?)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }

        double distance = kDefaultFocusDistance;
        if (info.Length() > 1 && info[1]->IsNumber()) {
            const double requested = v8pp::from_v8<double>(isolate, info[1]);
            distance               = std::isfinite(requested) ? std::clamp(requested, 1.0, 4096.0) : distance;
        }

        const SDK::Vec3 position = service->GetCamera().GetPose().position;
        service->GetCamera().SetPose(position, SDK::Vec3 {target.x - position.x, target.y - position.y, target.z - position.z});
        service->GetCamera().Focus(target, static_cast<float>(distance));
        info.GetReturnValue().Set(true);
    }

    void NoClip::JS_GetSpeed(const v8::FunctionCallbackInfo<v8::Value> &info) {
        KCDC::Features::Camera::CameraControlService *service = Service();
        info.GetReturnValue().Set(service != nullptr ? static_cast<double>(service->GetCamera().GetSpeed()) : 0.0);
    }

    void NoClip::JS_SetSpeed(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setSpeed: expected (metresPerSecond)"));
            return;
        }

        const double speed = v8pp::from_v8<double>(isolate, info[0]);
        if (!std::isfinite(speed)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setSpeed: the speed must be a finite number of metres a second"));
            return;
        }

        if (KCDC::Features::Camera::CameraControlService *service = Service()) {
            service->GetCamera().SetSpeed(static_cast<float>(speed));
        }
    }

    void NoClip::JS_GetFov(const v8::FunctionCallbackInfo<v8::Value> &info) {
        KCDC::Features::Camera::CameraControlService *service = Service();
        info.GetReturnValue().Set(service != nullptr ? static_cast<double>(service->GetCamera().GetRequestedFieldOfView()) : 0.0);
    }

    void NoClip::JS_SetFov(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setFov: expected (degrees)"));
            return;
        }

        const double degrees = v8pp::from_v8<double>(isolate, info[0]);
        if (!std::isfinite(degrees)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setFov: the field of view must be a finite number of degrees"));
            return;
        }

        if (KCDC::Features::Camera::CameraControlService *service = Service()) {
            service->GetCamera().SetFieldOfView(static_cast<float>(degrees));
        }
    }

    void NoClip::JS_IsInputEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        KCDC::Features::Camera::CameraControlService *service = Service();
        info.GetReturnValue().Set(service != nullptr && service->IsBuiltInInputEnabled());
    }

    void NoClip::JS_SetInputEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.setInputEnabled: expected (enabled)"));
            return;
        }

        if (KCDC::Features::Camera::CameraControlService *service = Service()) {
            service->SetBuiltInInputEnabled(info[0]->BooleanValue(isolate));
        }
    }

    void NoClip::JS_Move(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 2 || !info[0]->IsObject() || !info[1]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.move: expected (input, deltaSeconds)"));
            return;
        }

        const double deltaSeconds = v8pp::from_v8<double>(isolate, info[1]);
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.move: deltaSeconds must be a finite number of seconds, and not negative"));
            return;
        }

        KCDC::Features::Camera::FlyInput fly {};
        fly.forward    = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "forward", 0.0), -1.0, 1.0));
        fly.strafe     = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "strafe", 0.0), -1.0, 1.0));
        fly.lift       = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "lift", 0.0), -1.0, 1.0));
        fly.yawDelta   = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "yaw", 0.0), -180.0, 180.0));
        fly.pitchDelta = static_cast<float>(std::clamp(NumberOption(isolate, info, 0, "pitch", 0.0), -180.0, 180.0));
        fly.boost      = BoolOption(isolate, info, 0, "boost", false);
        fly.crawl      = BoolOption(isolate, info, 0, "crawl", false);

        KCDC::Features::Camera::CameraControlService *service = Service();
        info.GetReturnValue().Set(service != nullptr && service->Move(fly, static_cast<float>(deltaSeconds)));
    }

    void NoClip::JS_RayThroughScreen(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 2 || !info[0]->IsNumber() || !info[1]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.rayThroughScreen: expected (x, y)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        if (service == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }

        const double x = v8pp::from_v8<double>(isolate, info[0]);
        const double y = v8pp::from_v8<double>(isolate, info[1]);
        if (!std::isfinite(x) || !std::isfinite(y)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.rayThroughScreen: both coordinates must be finite"));
            return;
        }

        const SDK::Vec3 heading = KCDC::Features::Camera::RayThroughNdc(service->GetCamera().GetPose(), static_cast<float>(x), static_cast<float>(y));
        info.GetReturnValue().Set(VectorToV8(isolate, heading));
    }

    void NoClip::JS_ProjectToScreen(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::Vec3 point {};
        if (!ReadPoint(info, 0, point)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "NoClip.projectToScreen: expected (point)"));
            return;
        }

        KCDC::Features::Camera::CameraControlService *service = ActiveService();
        float ndcX                                     = 0.0F;
        float ndcY                                     = 0.0F;
        if (service == nullptr || !KCDC::Features::Camera::ProjectToNdc(service->GetCamera().GetPose(), point, ndcX, ndcY)) {
            info.GetReturnValue().SetNull();
            return;
        }

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> result   = v8::Object::New(isolate);
        result->Set(context, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, ndcX)).Check();
        result->Set(context, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, ndcY)).Check();
        info.GetReturnValue().Set(result);
    }

    void Events::NoClipChanged(bool active, const char *reason) {
        const std::string named = reason != nullptr ? reason : "unknown";
        EmitReservedEvent("noclipChanged", [active, named](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, active), v8pp::to_v8(isolate, named)};
        });
    }

    void NoClip::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("noclipChanged", "[active: boolean, reason: \"script\" | \"mapEditor\" | \"viewLost\" | \"sessionOver\"]",
            "Dispatched when the flight starts and when it ends, on this machine only. Handler promises are not awaited.\n\n"
            "`reason` is `script` on the way in. On the way out it is `script` for a resource ending it, `mapEditor` for the map editor taking the one camera, `viewLost` for the level going away underneath the camera's anchor, and `sessionOver` for the session ending.");

        auto &state = ClientCatalog().data_type("NoClipState", "Where the free camera is and what it is doing.");
        state.add_property("mode", "\"body\" | \"camera\"", "Whether the player's body is carried through the world or left behind.");
        state.add_property("input", "boolean", "Whether this machine's own keyboard and mouse are flying the camera.");
        state.add_property("position", "Vector3", "Where the camera is, in world-space metres.");
        state.add_property("forward", "Vector3", "The unit direction it is looking along.");
        state.add_property("right", "Vector3", "The unit direction to its right, level with the horizon.");
        state.add_property("up", "Vector3", "The unit direction out of the top of the frame.");
        state.add_property("yaw", "number", "Its heading in degrees, wrapped to -180..180. Zero faces +Y and a rising yaw swings left.");
        state.add_property("pitch", "number", "Its elevation in degrees, clamped to -89..89. Positive looks up.");
        state.add_property("speed", "number", "Metres a second at rest, before boost or crawl scales it.");
        state.add_property("fov", "number", "The vertical field of view in degrees the view was asked for.");
        state.add_property("renderedFov", "number", "The vertical field of view the renderer settled on, which `Hor+` adaptation and the window's aspect both move.");
        state.add_property("aspectRatio", "number", "Width over height, as the renderer reports it.");

        v8pp::module noclip(isolate, ClientCatalog(), "NoClip",
            "This machine's free camera, and what happens to the player's body while it is flying.\n\n"
            "The view is a bodyless anchor entity made the active camera through the engine's own view system, so nothing is ever asked to sweep against the world. In `body` mode the player's entity is taken out of the physical world and placed at the camera each frame.\n\n"
            "Client-only, and nothing here is an order another machine takes. It is not invisible to the session either: in `body` mode the thing being flown is the entity this client replicates, so the other players watch the flier go. In `camera` mode the body never moves.\n\n"
            "The game's controls are held for as long as the flight lasts, whichever side is driving. The map editor (`MapEditor`) flies the same one camera: a flight in progress ends when the editor opens, and `enable` refuses while it is open.");

        noclip.function("enable", &NoClip::JS_Enable,
            v8pp::metadata::docs("\"enabled\" | \"alreadyActive\" | \"noCamera\" | \"cameraBusy\"",
                {
                    v8pp::metadata::param("options", "{ mode?: \"body\" | \"camera\"; input?: boolean; speed?: number; fov?: number }", true,
                        "`mode` decides what happens to the player. `body`, the default, carries them: they pass through geometry, the world streams around them, the other players see them fly, and they land wherever the flight ends. `camera` leaves the body where it stands and touches nothing about its physics. A name that is neither is rejected rather than guessed at.\n\n"
                        "`input` is whether this machine's keyboard and mouse fly the camera -- on the keys the game's own photo mode is bound to in the controls menu (forward, back, left, right, jump to rise, crouch to sink, fast movement to boost), read through the active keyboard layout so AZERTY and the like work as the game does; Alt to crawl, mouse to turn. On by default; off, drive it with `move`.\n\n"
                        "`speed` is metres a second at rest, 0.05 to 400 and 12 by default. `fov` is the vertical field of view in degrees, up to 140; omitted, the view keeps the game's."),
                },
                "Starts flying. The camera begins where the player was looking, so this reads as stopping rather than as a cut.",
                "`enabled` when the flight started. `alreadyActive` when one was already running -- the options are not re-applied. `noCamera` when there is no level to spawn the camera's anchor into. `cameraBusy` when the map editor holds the camera."));

        noclip.function("disable", &NoClip::JS_Disable,
            v8pp::metadata::docs("void",
                {
                    v8pp::metadata::param("options", "{ keepPosition?: boolean }", true,
                        "`keepPosition` leaves the body where the camera stopped, and is the default. False puts it back where it took off from, facing the way it was. Neither means anything in `camera` mode."),
                },
                "Ends the flight, hands the view back and puts the body back into the physical world. Does nothing when no flight is running.", "Nothing. `noclipChanged` reports the end with reason `script`."));

        noclip.function("isActive", &NoClip::JS_IsActive, v8pp::metadata::docs("boolean", {}, "Whether a flight is running at this machine.", "True while the free camera has the view."));

        noclip.function("getState", &NoClip::JS_GetState, v8pp::metadata::docs("NoClipState | null", {}, "Everything about the flight in one read, rather than six calls that could each land on a different frame.", "The state, or null when no flight is running."));

        noclip.function("getPosition", &NoClip::JS_GetPosition, v8pp::metadata::docs("Vector3 | null", {}, "Where the camera is, in world-space metres.", "Its position, or null when no flight is running."));

        noclip.function("setPosition", &NoClip::JS_SetPosition,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("position", "Vector3", false, "Where to put it, in world-space metres. A non-finite position is ignored.")}, "Puts the camera somewhere else without turning it.",
                "True when the camera took the position; false when no flight is running."));

        noclip.function("getRotation", &NoClip::JS_GetRotation,
            v8pp::metadata::docs("{ yaw: number; pitch: number } | null", {}, "How the camera is aimed, in degrees. Two angles rather than a quaternion: that is what flying with a mouse produces, and it keeps the horizon level for free.",
                "Its angles, or null when no flight is running."));

        noclip.function("setRotation", &NoClip::JS_SetRotation,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("rotation", "{ yaw?: number; pitch?: number }", false, "The angles to take, in degrees. Either may be left out to keep the one the camera has. Yaw wraps to -180..180 and pitch is clamped to -89..89.")}, "Aims the camera without moving it.",
                "True when the camera took the angles; false when no flight is running."));

        noclip.function("getForward", &NoClip::JS_GetForward, v8pp::metadata::docs("Vector3 | null", {}, "The unit direction the camera is looking along.", "Its view direction, or null when no flight is running."));

        noclip.function("setPose", &NoClip::JS_SetPose,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("position", "Vector3", false, "Where to put the camera, in world-space metres."),
                    v8pp::metadata::param("forward", "Vector3", false, "The direction to aim it along. Need not be normalized; a direction of no length is ignored."),
                },
                "Places and aims the camera in one go.", "True when the camera took the pose; false when no flight is running."));

        noclip.function("lookAt", &NoClip::JS_LookAt,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("point", "Vector3", false, "The world-space point to face.")}, "Turns the camera to face a point, leaving it where it is.", "True when the camera turned; false when no flight is running."));

        noclip.function("focus", &NoClip::JS_Focus,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("target", "Vector3", false, "The world-space point to look at."),
                    v8pp::metadata::param("distance", "number", true, "How far back to stand from it, in metres, from 1 to 4096. 6 by default."),
                },
                "Turns to face a point and then stands back from it, which is what framing an object wants.", "True when the camera moved; false when no flight is running."));

        noclip.function("getSpeed", &NoClip::JS_GetSpeed, v8pp::metadata::docs("number", {}, "Metres a second the camera flies at rest, before boost or crawl scales it.", "The speed, or 0 when there is no application to ask."));

        noclip.function("setSpeed", &NoClip::JS_SetSpeed,
            v8pp::metadata::docs("void", {v8pp::metadata::param("metresPerSecond", "number", false, "The new speed, clamped to 0.05..400.")}, "Sets how fast the camera flies. Kept across flights, so a resource can set it once.", "Nothing."));

        noclip.function("getFov", &NoClip::JS_GetFov,
            v8pp::metadata::docs("number", {}, "The vertical field of view in degrees the view was last asked for, which is not what came back: `getState` reports the rendered one separately as `renderedFov`.", "The requested field of view."));

        noclip.function("setFov", &NoClip::JS_SetFov, v8pp::metadata::docs("void", {v8pp::metadata::param("degrees", "number", false, "The vertical field of view, clamped to 20..140.")}, "Sets the camera's lens.", "Nothing."));

        noclip.function("isInputEnabled", &NoClip::JS_IsInputEnabled, v8pp::metadata::docs("boolean", {}, "Whether this machine's own keyboard and mouse are flying the camera.", "True while the built-in driver is on."));

        noclip.function("setInputEnabled", &NoClip::JS_SetInputEnabled,
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "True to let this machine's keyboard and mouse fly the camera, false to leave it to `move`.")},
                "Hands the camera between the built-in driver and the resource, mid-flight. The game's controls stay held either way: the choice is who flies the camera, not whether the player can walk.", "Nothing."));

        noclip.function("move", &NoClip::JS_Move,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("input", "{ forward?: number; strafe?: number; lift?: number; yaw?: number; pitch?: number; boost?: boolean; crawl?: boolean }", false,
                        "One frame of flight. `forward`, `strafe` and `lift` are -1..1 along the view's heading, its right and world up -- lift is along world up rather than the view's, so rising and falling stay vertical while looking down. `yaw` and `pitch` are this frame's turn in degrees. `boost` multiplies the speed by six and `crawl` divides it by five, as Shift and Alt do."),
                    v8pp::metadata::param("deltaSeconds", "number", false, "How long this frame is. Anything over 0.1 is treated as 0.1, so a stalled frame does not launch the camera across the map."),
                },
                "Moves the camera by one frame of flight the resource resolved itself. Refused while the built-in driver is on, so the two cannot both integrate the same frame.",
                "True when the frame was applied. False when no flight is running and when the built-in driver has the camera."));

        noclip.function("rayThroughScreen", &NoClip::JS_RayThroughScreen,
            v8pp::metadata::docs("Vector3 | null",
                {
                    v8pp::metadata::param("x", "number", false, "Across the frame, -1 at the left edge and +1 at the right."),
                    v8pp::metadata::param("y", "number", false, "Up the frame, -1 at the bottom and +1 at the top."),
                },
                "The world direction through a point on the camera's near plane.\n\nNormalized device coordinates rather than pixels, because the mod does not own the back buffer's size. Pair it with `World.raycast` from `getPosition()` to find what is under a point of the frame.",
                "A unit direction, or null when no flight is running."));

        noclip.function("projectToScreen", &NoClip::JS_ProjectToScreen,
            v8pp::metadata::docs("{ x: number; y: number } | null", {v8pp::metadata::param("point", "Vector3", false, "The world-space point to project.")},
                "Where a world point lands on the camera's frame, in the same normalized device coordinates `rayThroughScreen` takes.", "Its place on the frame, or null when no flight is running and when the point is behind the camera."));

        noclip.publish(global);
    }
} // namespace KCDC::Scripting
