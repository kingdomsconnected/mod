#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /**
     * The global `Appearance`: the game's own character-component catalog.
     *
     * Browsing only. Putting a body on somebody is `player.setAppearance`,
     * because the owning client is the only peer that may publish its own
     * body, and the verb that asks it belongs where the connection is.
     */
    class Appearance {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Options(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Beards(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Random(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
