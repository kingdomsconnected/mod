#pragma once

#include <v8.h>

namespace KCDC::Core {
    /**
     * Publishes the server's whole scripting surface: one line per feature,
     * and the only place the shell knows the bindings exist.
     *
     * Reached from Server::ModuleRegister. Player and Horse each register the
     * framework class they inherit for themselves, in GetClass.
     */
    void RegisterScripting(v8::Isolate *isolate, v8::Local<v8::Object> global);

    /** Teaches ValueTransfer every KCDC handle, so one crosses between resources as itself. Once, in PostInit. */
    void RegisterTransfers();

    /** Drops every KCDC class wrapper held for a resource runtime that is going away. */
    void UnregisterGlobals(v8::Isolate *isolate);
} // namespace KCDC::Core
