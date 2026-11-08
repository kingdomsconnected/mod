#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /** Publishes `Controls`: the mod's input hold and its cursor, for resources. */
    void RegisterControls(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
