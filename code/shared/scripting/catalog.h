#pragma once

#include <scripting/scripting_catalog.h>

#include <v8pp/metadata.hpp>

#include <string>
#include <string_view>

namespace KCDC::Scripting {
    // Keep project metadata separate; export merges the framework catalog.
    inline v8pp::metadata::registry &ServerCatalog() {
        return v8pp::metadata::catalog("kcdc-server");
    }

    inline v8pp::metadata::registry &ClientCatalog() {
        return v8pp::metadata::catalog("kcdc-client");
    }

    // Creates EventMap, which every feature binding appends its own
    // events to -- so this has to run before any of them. The
    // framework's own events are blended in by ExportCatalog rather
    // than restated here: a copy of a tuple the framework owns goes
    // stale the moment it changes, and silently.
    inline void EnsureEventMap(v8pp::metadata::registry &catalog) {
        catalog.data_type("EventMap", "Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.");
    }

    // Export project and framework metadata together. The framework's
    // Events object comes across with the rest: it is recorded per side,
    // so it is the only declaration of the bus that knows the client has
    // emitServer and the server emitAllClients and onClient.
    inline bool ExportCatalog(v8pp::metadata::registry &mod, std::string_view frameworkCatalog, const std::string &path) {
        Framework::Scripting::MergeScriptingCatalog(mod, v8pp::metadata::catalog(frameworkCatalog));
        return v8pp::metadata::write_json_file(mod, path);
    }
} // namespace KCDC::Scripting
