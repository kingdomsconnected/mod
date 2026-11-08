#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    // The global `Audio`: the game's own sound triggers, played from a client
    // resource. Client-side and unreplicated, which is why there is no
    // server-side twin -- a sound everyone should hear is one the server tells
    // every client to play. Every verb is one call into
    // KCDC::Features::Audio::AudioService and returns whether the game took it.
    class Audio {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Play(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_PlayAt(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_PlayOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_PlayFile(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_StopOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_StopAllOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetEntityParameter(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetEntitySwitch(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_HasTrigger(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
