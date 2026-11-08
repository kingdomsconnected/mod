#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    // The global `Vfx`: the game's own particle effects, played on this
    // machine. Client-side and unreplicated, exactly like `Audio` -- an effect
    // everyone should see is one the server tells every client to play, or one
    // the server places as a replicated effect and lets the interest grid
    // stream. Every verb is one call into KCDC::Features::Vfx::VfxService.
    class Vfx {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Attach(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Stop(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_StopAll(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlaying(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Has(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_List(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Count(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
