#include "camera_scripting.h"

// Before the replication headers: they bring WinSock2, which has to precede
// whatever pulls in Windows.h.
#include <utils/safe_win32.h>

#include "camera_view.h"

#include "core/application.h"
#include "features/camera/camera_control_service.h"

#include "common/module_support.h"

#include "shared/scripting/camera_options.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace KCDC::Scripting {
    namespace {
        v8::Local<v8::Object> PoseToObject(v8::Isolate *isolate, const KCDC::Features::Camera::ViewPose &pose) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.position))).Check();
            result->Set(context, v8pp::to_v8(isolate, "forward"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.forward))).Check();
            result->Set(context, v8pp::to_v8(isolate, "right"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.right))).Check();
            result->Set(context, v8pp::to_v8(isolate, "up"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.up))).Check();
            result->Set(context, v8pp::to_v8(isolate, "fov"), v8pp::to_v8(isolate, pose.fovDegrees)).Check();
            result->Set(context, v8pp::to_v8(isolate, "aspectRatio"), v8pp::to_v8(isolate, pose.aspectRatio)).Check();
            return result;
        }

        /** One optional finite number off an options bag, clamped to a range. */
        float ReadAxis(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *name) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return 0.0F;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Value> value;
            if (!info[index].As<v8::Object>()->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&value) || !value->IsNumber()) {
                return 0.0F;
            }
            const double axis = v8pp::from_v8<double>(isolate, value);
            return std::isfinite(axis) ? std::clamp(static_cast<float>(axis), -1.0F, 1.0F) : 0.0F;
        }

        // JS: Camera.getPose() -> CameraPose | null
        void JS_GetPose(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Camera::ViewPose pose;
            if (!KCDC::Features::Camera::ReadViewPose(pose)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(PoseToObject(isolate, pose));
        }

        // JS: Camera.screenRay(options?) -> CameraRay | null
        void JS_ScreenRay(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Camera::ViewPose pose;
            if (!KCDC::Features::Camera::ReadViewPose(pose)) {
                info.GetReturnValue().SetNull();
                return;
            }

            const float ndcX = ReadAxis(isolate, info, 0, "x");
            const float ndcY = ReadAxis(isolate, info, 0, "y");

            float range = 100.0F;
            if (info.Length() > 0 && info[0]->IsObject()) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Value> value;
                if (info[0].As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "range")).ToLocal(&value) && value->IsNumber()) {
                    const double asked = v8pp::from_v8<double>(isolate, value);
                    if (std::isfinite(asked)) {
                        range = std::clamp(static_cast<float>(asked), 0.1F, 4096.0F);
                    }
                }
            }

            const SDK::Vec3 direction = KCDC::Features::Camera::RayThroughNdc(pose, ndcX, ndcY);
            const SDK::Vec3 target {pose.position.x + direction.x * range, pose.position.y + direction.y * range, pose.position.z + direction.z * range};

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "origin"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.position))).Check();
            result->Set(context, v8pp::to_v8(isolate, "direction"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(direction))).Check();
            result->Set(context, v8pp::to_v8(isolate, "target"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(target))).Check();
            info.GetReturnValue().Set(result);
        }

        // JS: Camera.setThirdPerson(options | null) -> void
        void JS_SetThirdPerson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *app = KCDC::Core::gApplication.get();
            if (app == nullptr) {
                return;
            }
            KCDC::Features::Camera::CameraControlService &camera = app->GetCameraControl();
            if (info.Length() >= 1 && (info[0]->IsNull() || info[0]->IsFalse())) {
                camera.DisableThirdPerson();
                return;
            }
            if (info.Length() >= 1 && !info[0]->IsUndefined() && !info[0]->IsObject() && !info[0]->IsTrue()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Camera.setThirdPerson: expected (options | null)"));
                return;
            }

            KCDC::Features::Camera::ThirdPersonOptions options;
            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Object> bag = info[0].As<v8::Object>();
                FloatOption(isolate, bag, "distance", options.distance);
                FloatOption(isolate, bag, "height", options.height);
                FloatOption(isolate, bag, "shoulder", options.shoulder);
            }
            camera.EnableThirdPerson(options);
        }

        // JS: Camera.setShot(options) -> boolean
        void JS_SetShot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            CameraShotOptions asked;
            if (!ParseCameraShotOptions(info, 0, asked)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Camera.setShot: expected (") + kCameraShotOptionsType + ")"));
                return;
            }

            auto *app = KCDC::Core::gApplication.get();
            if (app == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            KCDC::Features::Camera::ShotOptions options;
            options.position    = KCDC::Common::ToNative(asked.position);
            options.lookAt      = KCDC::Common::ToNative(asked.lookAt);
            options.durationMs  = asked.durationMs;
            options.fieldOfView = asked.fieldOfView;
            info.GetReturnValue().Set(app->GetCameraControl().SetShot(options));
        }

        // JS: Camera.clearShot(duration?) -> void
        void JS_ClearShot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *app = KCDC::Core::gApplication.get()) {
                app->GetCameraControl().ClearShot(GlideDurationArg(info, 0));
            }
        }

        // JS: Camera.isShotActive() -> boolean
        void JS_IsShotActive(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *app = KCDC::Core::gApplication.get();
            info.GetReturnValue().Set(app != nullptr && app->GetCameraControl().IsShotActive());
        }

        // JS: Camera.isThirdPerson() -> boolean
        void JS_IsThirdPerson(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *app = KCDC::Core::gApplication.get();
            info.GetReturnValue().Set(app != nullptr && app->GetCameraControl().IsThirdPerson());
        }
    } // namespace

    void RegisterCamera(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &pose = ClientCatalog().data_type("CameraPose", "Where this client is drawing from, as the renderer settled on it this frame.");
        pose.add_property("position", "Vector3", "The eye position, in world-space metres.");
        pose.add_property("forward", "Vector3", "Where it looks, as a unit vector.");
        pose.add_property("right", "Vector3", "Its own right, as a unit vector. Read off the camera rather than rebuilt from a yaw, so it carries roll.");
        pose.add_property("up", "Vector3", "Its own up, as a unit vector.");
        pose.add_property("fov", "number", "Vertical field of view in degrees. What the renderer settled on, which Hor+ adaptation and the window's aspect both move -- not what the game asked for.");
        pose.add_property("aspectRatio", "number", "Width over height.");

        auto &ray = ClientCatalog().data_type("CameraRay", "A ray out of the camera, ready to hand to `World.raycast`.");
        ray.add_property("origin", "Vector3", "Where it starts: the eye position.");
        ray.add_property("direction", "Vector3", "Where it points, as a unit vector.");
        ray.add_property("target", "Vector3", "Where it ends, which is `origin` plus `direction` times the range asked for.");

        v8pp::module camera(isolate, ClientCatalog(), "Camera",
            "The view this client is drawing through. Nothing here is replicated and nothing here is authority: it describes one machine's own camera at one moment.\n\n"
            "This is the game's camera, not a camera of the mod's own -- it follows the player, a dialogue, a cutscene or a free flight, whichever the game currently has. `NoClip` is what takes it over, `setThirdPerson` puts it behind the player, and `setShot` stands it somewhere in the world.");

        camera.function("getPose", &JS_GetPose,
            v8pp::metadata::docs("CameraPose | null", {}, "Where the camera is this frame, and what it can see from there.", "The pose, or null in the main menu and across a level load, when there is no active view at all."));

        camera.function("screenRay", &JS_ScreenRay,
            v8pp::metadata::docs("CameraRay | null",
                {v8pp::metadata::param("options", "{ x?: number; y?: number; range?: number }", true,
                    "`x` and `y` are a point on screen in normalized device coordinates -- both run -1 at the left and bottom to +1 at the right and top, so the default (0, 0) is the centre of the screen, which is where the crosshair is. `range` is how far out to carry `target`, in metres, 100 by default and up to 4096.")},
                "A ray out of the camera through a point on screen. This is what turns \"what is the player looking at\" into the two points `World.raycast` takes.",
                "The ray, or null when there is no active view."));

        camera.function("setThirdPerson", &JS_SetThirdPerson,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("options", "{ distance?: number; height?: number; shoulder?: number } | null", true,
                    "Where the camera sits, in metres: `distance` back from the body along where the player looks (3.5 by default, 0.3 to 20), `height` of the point it orbits above the body's feet (1.6, up to 5 either way), and `shoulder` to the right of it, negative for the left (0, up to 3 either way). Null hands the view back to the game's own camera. Called again while on, it only moves the camera.")},
                "Puts this player's camera behind them, for as long as it is on.\n\nThe game has no third-person view to switch to, so this is a camera of the mod's own, placed every frame behind the body along the direction the player is looking. Nothing about their controls changes: they walk and turn as they did, and the mouse still turns the body. When a wall is behind them the camera comes in along the same line rather than going through it. Their head, which the game keeps out of its own first person, is drawn for as long as this camera is the one looking at them.\n\nThis is what a disguised player needs -- `player.setDisguise` hides the body the game's own camera sits in. It is this client's own view, so a gamemode turns it on from a client script, typically on an event the server sends the disguised player.\n\nThere is one view to draw through: `NoClip` cannot start while this holds it, and while `NoClip` flies this waits. The map, the inventory and the other pause-screen pages are filmed by the game's own camera, so this steps aside while one is open and comes back when it closes. It lasts until the session ends."));

        camera.function("isThirdPerson", &JS_IsThirdPerson,
            v8pp::metadata::docs("boolean", {}, "Whether `setThirdPerson` is on.", "True from `setThirdPerson` until it is turned off, also while it waits for a body or for `NoClip`."));

        camera.function("setShot", &JS_SetShot,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("options", kCameraShotOptionsType, false,
                    "`position` is where the camera stands and `lookAt` the point it faces, both in world space. `duration` is how long it takes to get there from wherever the view is now, in milliseconds: 0, the default, is a cut, and the most is 60000. `fov` is the vertical field of view in degrees, 20 to 120; leave it out to keep the player's own.")},
                "Films this player's view from a fixed point: what a cutscene, an intro or a kill cam is made of.\n\n"
                "The camera holds there until `clearShot` -- or until another `setShot`, which glides on from wherever this one is. Nothing about the player's controls changes; a gamemode that wants them still says so through `Controls`. The player's head, which the game keeps out of its first person, is drawn for as long as the camera is looking at them from outside their eyes.\n\n"
                "There is one view to draw through. A shot outranks `setThirdPerson`, which takes over again when the shot ends, and `NoClip` cannot start while it holds. The map, the inventory and the other pause-screen pages are filmed by the game's own camera, so the shot steps aside while one is open and comes back when it closes. A shot ends with the session.\n\n"
                "The server can send the same shot with its own `Camera.setShot(player, options)`; whichever comes last wins.",
                "True, or false while `NoClip` is flying or when `position` and `lookAt` are the same point."));

        camera.function("clearShot", &JS_ClearShot,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("duration", "number", true,
                    "How long the glide back into the player's eyes takes, in milliseconds: 0, the default, is a cut, and the most is 60000. The glide follows the player if they move meanwhile. With `setThirdPerson` on, it ends behind them instead and their head stays drawn.")},
                "Gives the player their own view back. Does nothing without a shot."));

        camera.function("isShotActive", &JS_IsShotActive,
            v8pp::metadata::docs("boolean", {}, "Whether a shot holds the view.", "True from `setShot`, including its glide in, until `clearShot`; false already while gliding back."));

        camera.publish(global);
    }
} // namespace KCDC::Scripting
