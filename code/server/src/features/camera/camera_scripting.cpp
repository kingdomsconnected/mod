#include "camera_scripting.h"

#include "features/player/player_scripting.h"

#include "shared/features/camera/camera_rpc.h"
#include "shared/scripting/camera_options.h"
#include "shared/scripting/catalog.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <string>

namespace KCDC::Scripting::Camera {
    namespace {
        /** The connected player in the first slot, or null after throwing `error`. */
        Shared::Entities::HumanEntity *PlayerArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *error) {
            auto *player = info.Length() >= 1 ? v8pp::class_<KCDC::Scripting::Player>::unwrap_object(info.GetIsolate(), info[0]) : nullptr;
            auto *human  = player != nullptr ? player->ResolveHuman() : nullptr;
            if (human == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), error));
            }
            return human;
        }

        /** Reliable and ordered: a shot and the clear that follows it must not arrive the other way round. */
        bool Send(Shared::RPC::CameraShot &shot, const Shared::Entities::HumanEntity &human) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (net == nullptr) {
                return false;
            }
            net->SendRPC(shot, MafiaNet::ToGuid(human.ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            return true;
        }

        // JS: Camera.setShot(player, options) -> boolean
        void JS_SetShot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Shared::Entities::HumanEntity *human = PlayerArg(info, "Camera.setShot requires a connected Player");
            if (human == nullptr) {
                return;
            }

            CameraShotOptions asked;
            if (!ParseCameraShotOptions(info, 1, asked)) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Camera.setShot: expected (player, ") + kCameraShotOptionsType + ")"));
                return;
            }

            Shared::RPC::CameraShot shot;
            shot.active      = true;
            shot.position    = asked.position;
            shot.lookAt      = asked.lookAt;
            shot.durationMs  = asked.durationMs;
            shot.fieldOfView = asked.fieldOfView;
            if (!shot.IsSane()) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(Send(shot, *human));
        }

        // JS: Camera.clearShot(player, duration?) -> boolean
        void JS_ClearShot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Shared::Entities::HumanEntity *human = PlayerArg(info, "Camera.clearShot requires a connected Player");
            if (human == nullptr) {
                return;
            }

            Shared::RPC::CameraShot shot;
            shot.active     = false;
            shot.durationMs = GlideDurationArg(info, 1);
            info.GetReturnValue().Set(Send(shot, *human));
        }
    } // namespace

    void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module camera(isolate, ServerCatalog(), "Camera",
            "One player's view, directed from the server. A shot is sent and forgotten: the server keeps no record of it, so a player who reconnects or changes level has their own view until they are sent another.");

        camera.function("setShot", &JS_SetShot,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("player", "Player", false, "Whose view to film."),
                    v8pp::metadata::param("options", kCameraShotOptionsType, false,
                        "`position` is where the camera stands and `lookAt` the point it faces, both in world space. `duration` is how long it takes to get there from wherever the player's view is, in milliseconds: 0, the default, is a cut, and the most is 60000. `fov` is the vertical field of view in degrees, 20 to 120; leave it out to keep the player's own.")},
                "Films one player's view from a fixed point: what a cutscene, an intro or a kill cam is made of. It holds until `clearShot`, or glides on to the next `setShot`.\n\n"
                "This is the client's `Camera.setShot`, sent to that player; whichever of the two comes last wins. Nothing about the player's controls changes, and nothing about where their body is: their character stays where it stood, drawn head and all for as long as the camera is looking at it from outside its eyes. A player flying with `NoClip` refuses the shot.",
                "True when the shot was sent, false when `position` and `lookAt` are the same point."));

        camera.function("clearShot", &JS_ClearShot,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("player", "Player", false, "Whose view to give back."),
                    v8pp::metadata::param("duration", "number", true, "How long the glide back into the player's eyes takes, in milliseconds: 0, the default, is a cut, and the most is 60000.")},
                "Gives a player their own view back. Harmless for a player without a shot.", "True when it was sent."));

        camera.publish(global);
    }
} // namespace KCDC::Scripting::Camera
