#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    // The global `Navigation`: the game's own navigation mesh, when the server
    // was given one. Every query answers from memory on the tick, with no
    // client involved; without a mesh each answers null or false and `ready`
    // says why.
    class Navigation final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_ClosestPoint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_FloorAt(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ValidatePatrol(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_FindPath(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_CanReach(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Raycast(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_RandomPoint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_EnableOverlay(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_DisableOverlay(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
