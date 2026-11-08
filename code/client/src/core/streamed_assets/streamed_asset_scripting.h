#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    // The global `Assets`: what the server streams, seen from a client resource. Everything a server
    // streams loads by path like the game's own, so this only answers what is there.
    class Assets {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Has(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting
