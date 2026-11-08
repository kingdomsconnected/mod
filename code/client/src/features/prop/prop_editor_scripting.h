#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /**
     * Publishes the `MapEditor` global: the world builder, opened and closed
     * through scripts or F7, with per-player access during multiplayer.
     */
    void RegisterMapEditor(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
