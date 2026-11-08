#pragma once

#include <v8.h>

namespace KCDC::Scripting::Camera {
    /** Publishes the server's `Camera` global: shots sent to one player's view. */
    void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting::Camera
