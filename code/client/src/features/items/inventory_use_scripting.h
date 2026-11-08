#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    void RegisterInventoryUse(v8::Isolate *isolate, v8::Local<v8::Object> global);
}
