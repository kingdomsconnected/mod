#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /** Publishes the `Follow` global: the local player following a nearby player or human NPC. */
    void RegisterFollow(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
