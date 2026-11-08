#pragma once

#include <v8.h>

namespace KCDC::Core {
    /**
     * Publishes the client's whole scripting surface: one line per feature,
     * and the only place the shell knows the bindings exist.
     *
     * Reached from Application::ModuleRegister, which runs on connect rather
     * than on boot -- client scripting comes up with a session and again with
     * the next one -- so everything here registers against whichever isolate
     * that call is handed.
     */
    void RegisterScripting(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Core
