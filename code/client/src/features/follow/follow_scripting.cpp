#include "follow_scripting.h"

#include "core/scripting/script_context.h"
#include "features/player/player_scripting.h"
#include "follow_service.h"
#include "shared/scripting/catalog.h"

#include <cmath>
#include <scripting/v8pp_types.h>
#include <utils/safe_win32.h>
#include <v8pp/module.hpp>

namespace KCDC::Features::Follow {
    namespace {
        v8::Local<v8::Object> Describe(v8::Isolate *isolate, const Target &target) {
            auto object  = v8::Object::New(isolate);
            auto context = isolate->GetCurrentContext();
            object->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, static_cast<double>(target.id))).Check();
            object->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, target.npc ? "npc" : "player")).Check();
            object->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, target.name)).Check();
            object->Set(context, v8pp::to_v8(isolate, "position"), v8pp::to_v8(isolate, target.position)).Check();
            return object;
        }
    } // namespace

    void FollowService::DrainNotices() {
        std::vector<Notice> notices;
        notices.swap(_notices);
        for (const auto &notice : notices) {
            KCDC::EmitReservedEvent(notice.started ? "followStarted" : "followStopped", [&](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args {Describe(isolate, notice.target)};
                if (!notice.started) {
                    args.push_back(v8pp::to_v8(isolate, notice.reason));
                }
                return args;
            });
        }
    }
} // namespace KCDC::Features::Follow

namespace KCDC::Scripting {
    namespace {
        bool ReadTarget(v8::Isolate *isolate, v8::Local<v8::Value> value, KCDC::Features::Follow::Target &target) {
            if (!value->IsObject()) {
                return false;
            }
            if (auto *player = v8pp::class_<Player>::unwrap_object(isolate, value)) {
                target.id = player->GetId();
                return true;
            }
            auto object  = value.As<v8::Object>();
            auto context = isolate->GetCurrentContext();
            v8::Local<v8::Value> id;
            v8::Local<v8::Value> kind;
            if (!object->Get(context, v8pp::to_v8(isolate, "id")).ToLocal(&id) || !id->IsNumber() || !object->Get(context, v8pp::to_v8(isolate, "kind")).ToLocal(&kind) || !kind->IsString()) {
                return false;
            }
            const auto number = id.As<v8::Number>()->Value();
            const auto name   = v8pp::from_v8<std::string>(isolate, kind);
            if (!std::isfinite(number) || number <= 0 || number > 9007199254740991.0 || std::floor(number) != number || (name != "npc" && name != "player")) {
                return false;
            }
            target.id  = static_cast<std::uint64_t>(number);
            target.npc = name == "npc";
            return true;
        }

        void JS_Targets(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto targets  = KCDC::Features::Follow::FollowService::Get().Targets();
            auto array    = v8::Array::New(isolate, static_cast<int>(targets.size()));
            for (std::size_t i = 0; i < targets.size(); ++i) {
                array->Set(isolate->GetCurrentContext(), static_cast<std::uint32_t>(i), KCDC::Features::Follow::Describe(isolate, targets[i])).Check();
            }
            info.GetReturnValue().Set(array);
        }

        void JS_CanStart(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Follow::Target target;
            info.GetReturnValue().Set(info.Length() > 0 && ReadTarget(info.GetIsolate(), info[0], target) && KCDC::Features::Follow::FollowService::Get().CanStart(target));
        }

        void JS_Start(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Follow::Target target;
            info.GetReturnValue().Set(info.Length() > 0 && ReadTarget(info.GetIsolate(), info[0], target) && KCDC::Features::Follow::FollowService::Get().Start(target, KCDC::ResolveCallingResource(info.GetIsolate())));
        }

        void JS_GetTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto *target = KCDC::Features::Follow::FollowService::Get().Current();
            if (target != nullptr) {
                info.GetReturnValue().Set(KCDC::Features::Follow::Describe(info.GetIsolate(), *target));
            }
            else {
                info.GetReturnValue().SetNull();
            }
        }

        void JS_IsPending(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(KCDC::Features::Follow::FollowService::Get().Pending());
        }

        void JS_Stop(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Follow::FollowService::Get().Release(KCDC::ResolveCallingResource(info.GetIsolate()), "script");
        }
    } // namespace

    void RegisterFollow(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        KCDC::Features::Follow::FollowService::Get().AttachResources(KCDC::ScriptResources());

        auto &catalog = ClientCatalog();
        auto &target  = catalog.data_type("FollowTarget", "A streamed player or human NPC. Its network identity is resolved again when follow starts; position and name are snapshots.");
        target.add_property("id", "number", "The target's network id.");
        target.add_property("kind", "\"player\" | \"npc\"", "Which replicated entity the id identifies.");
        target.add_property("name", "string", "Player nickname or NPC name.");
        target.add_property("position", "Vector3", "Current native body position, in world-space metres.");
        auto &events = catalog.data_type("EventMap");
        events.add_property("followStarted", "[target: FollowTarget]", "The local native follow action has entered. A successful start request may still fail before this event.");
        events.add_property("followStopped", "[target: FollowTarget, reason: \"script\" | \"input\" | \"targetLost\" | \"mountChanged\" | \"teleported\" | \"unavailable\" | \"interrupted\" | \"failed\" | \"resourceStopped\" | \"sessionEnded\" | \"worldChanged\"]",
            "A queued or active scripted follow ended on this client.");
        v8pp::module follow(isolate, catalog, "Follow",
            "Follow another player or a streamed human NPC using the local player's native walking or riding action. Native combat, interaction and mount restrictions still apply. Only one scripted follow can be queued or active. Movement input, target loss, mount changes and resource shutdown stop it. This controls only the local player; it does not command an NPC to follow.");
        follow.function("targets", &JS_Targets, v8pp::metadata::docs("FollowTarget[]", {}, "Lists streamed players and human NPCs within 20 metres. Use canStart to check whether one can currently be followed."));
        follow.function("canStart", &JS_CanStart,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("target", "Player | FollowTarget", false, "A remote player handle or a descriptor from targets().")}, "Checks the target and native follow restrictions, including the game's start distance."));
        follow.function("start", &JS_Start,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("target", "Player | FollowTarget", false, "Who the local player should follow.")},
                "Queues native follow for the calling resource. True means queued; followStarted confirms entry. False means the request was refused."));
        follow.function("getTarget", &JS_GetTarget, v8pp::metadata::docs("FollowTarget | null", {}, "The active scripted follow target, or null while idle or pending."));
        follow.function("isPending", &JS_IsPending, v8pp::metadata::docs("boolean", {}, "Whether a scripted follow is queued but has not entered yet."));
        follow.function("stop", &JS_Stop, v8pp::metadata::docs("void", {}, "Cancels the calling resource's queued or active follow. Other resources' follow actions are left alone."));
        follow.publish(global);
    }
} // namespace KCDC::Scripting
