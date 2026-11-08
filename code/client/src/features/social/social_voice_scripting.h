#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    // Adds setIndicatorVisible(bool) / isIndicatorVisible() and
    // isPlayerTalking(player) to the framework's global Voice: what a
    // resource needs to hide the mod's voice indicator and draw its own.
    class Voice {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_SetIndicatorVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsIndicatorVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlayerTalking(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
