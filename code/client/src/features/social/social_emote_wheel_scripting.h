#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /** Publishes `EmoteWheel`: which catalog gesture each slot of this client's wheel chooses. */
    void RegisterEmoteWheel(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
