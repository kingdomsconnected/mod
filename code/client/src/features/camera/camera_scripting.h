#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /** Publishes the `Camera` global: where this client is drawing from. */
    void RegisterCamera(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
