#pragma once

#include "core/application.h"

#include <integrations/client/scripting/module.h>
#include <scripting/builtins/events.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/v8_engine.h>

#include <v8.h>

#include <string>
#include <vector>

namespace KCDC {
    // The client scripting resource manager, or null outside a session.
    inline Framework::Scripting::ResourceManager *ScriptResources() {
        Core::Application *application = Core::gApplication.get();
        auto *module                   = application != nullptr ? application->GetScriptingModule() : nullptr;
        return module != nullptr ? module->GetResourceManager() : nullptr;
    }

    // The resource whose code is calling, in the order the framework's own resource-owned APIs resolve it: the handler's origin when one is passed, then the explicit context, then the V8 stack. Empty outside a session.
    inline std::string ResolveCallingResource(v8::Isolate *isolate, v8::Local<v8::Function> function = {}) {
        Framework::Scripting::ResourceManager *manager = ScriptResources();
        if (manager == nullptr) {
            return {};
        }
        if (!function.IsEmpty()) {
            std::string name = manager->GetResourceNameFromFunction(isolate, function);
            if (!name.empty()) {
                return name;
            }
        }
        std::string name = manager->GetCurrentResourceContext();
        if (!name.empty()) {
            return name;
        }
        return manager->GetResourceContextFromStack(isolate);
    }

    // Runs `body` as (isolate, context, resourceManager) inside the client scripting isolate, taking the lock and opening the scopes in the one order they are legal in. Client scripting lives only inside a session, so outside one this does nothing.
    template <typename Body>
    void WithScriptContext(Body &&body) {
        Core::Application *application = Core::gApplication.get();
        auto *module                   = application != nullptr ? application->GetScriptingModule() : nullptr;
        if (module == nullptr) {
            return;
        }

        auto *engine          = module->GetEngine();
        auto *resourceManager = module->GetResourceManager();
        if (engine == nullptr || resourceManager == nullptr) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        if (isolate == nullptr) {
            return;
        }

        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        if (context.IsEmpty()) {
            return;
        }
        v8::Context::Scope contextScope(context);

        body(isolate, context, resourceManager);
    }

    // Emits one reserved event to every loaded resource. `buildArgs(isolate)` runs inside the isolate and returns the argument vector, so a handle is only ever built once there is a session to emit into; outside one nothing runs at all.
    template <typename BuildArgs>
    void EmitReservedEvent(const char *eventName, BuildArgs &&buildArgs) {
        WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            std::vector<v8::Local<v8::Value>> args = buildArgs(isolate);
            resources->GetEvents().EmitReserved(isolate, context, eventName, args);
        });
    }

    // Emits one reserved event a handler may refuse, and says whether nobody did. Every handler runs and has returned before this does; one returning literal `false` refuses, and a handler that is async cannot. Outside a session nothing is asked, which is not a refusal.
    template <typename BuildArgs>
    bool EmitReservedEventSync(const char *eventName, BuildArgs &&buildArgs) {
        bool accepted = true;
        WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            std::vector<v8::Local<v8::Value>> args = buildArgs(isolate);
            accepted                               = resources->GetEvents().EmitReservedSync(isolate, context, eventName, args);
        });
        return accepted;
    }
} // namespace KCDC
