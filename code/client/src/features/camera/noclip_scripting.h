#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /**
     * The global `NoClip`: this machine's free camera, and what happens to
     * the player's body while it is flying.
     *
     * Client-only, and there is no `NoClip` on the server. Not invisible to
     * the session though: in `body` mode the thing being flown is the entity
     * this client replicates.
     */
    class NoClip {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Enable(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Disable(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsActive(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetState(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void JS_GetPosition(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetPosition(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetRotation(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetRotation(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetForward(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetPose(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_LookAt(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Focus(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void JS_GetSpeed(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetSpeed(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetFov(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetFov(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void JS_IsInputEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetInputEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Move(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void JS_RayThroughScreen(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ProjectToScreen(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    // What this feature tells loaded resources happened on this machine.
    // Nothing here mentions V8, so CameraControlService can report without depending
    // on the scripting layer's classes.
    namespace Events {
        /** The flight started or ended; does not await script promises. */
        void NoClipChanged(bool active, const char *reason);
    } // namespace Events
} // namespace KCDC::Scripting
