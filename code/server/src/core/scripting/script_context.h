#pragma once

#include "core/server.h"

#include <integrations/server/scripting/module.h>
#include <scripting/builtins/events.h>
#include <scripting/function_references.h>
#include <scripting/node_engine.h>
#include <scripting/resource/resource_manager.h>

#include <v8.h>

#include <vector>

namespace KCDC {
    // Runs `body` inside the scripting isolate, if there is one. Every entry point that reaches script has to take the lock, enter the isolate and open a handle and context scope, in that order; this keeps that order in one place. `body` is called as (isolate, context, resourceManager), and not at all when scripting is absent or not yet initialized.
    template <typename Body>
    void WithScriptContext(Body &&body) {
        auto *server = Server::Get();
        auto *module = server != nullptr ? server->GetScriptingModule() : nullptr;
        if (module == nullptr) {
            return;
        }

        auto *engine          = module->GetEngine();
        auto *resourceManager = module->GetResourceManager();
        if (engine == nullptr || resourceManager == nullptr || !engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        body(isolate, context, resourceManager);
    }

    // A promise a native answer settles later, held for the resource that asked for it. A resource runs in a runtime of its own, so the promise belongs to that resource's isolate, not the engine's: it is settled by entering that isolate, and a value built anywhere else is not one the resource can read.
    class PendingPromise final {
      public:
        PendingPromise(v8::Isolate *isolate, v8::Local<v8::Context> owner, v8::Local<v8::Promise::Resolver> promise)
            : _isolate(isolate), _resolver(new v8::Global<v8::Promise::Resolver>(isolate, promise)), _context(new v8::Global<v8::Context>(isolate, owner)) {}

        ~PendingPromise() {
            Release();
        }

        PendingPromise(const PendingPromise &)            = delete;
        PendingPromise &operator=(const PendingPromise &) = delete;

        // Settles the promise with `build(isolate, context)`, once, inside the asking resource. Nothing runs once that resource has stopped.
        template <typename Build>
        void Settle(Build &&build) {
            if (_resolver == nullptr || !OwnerAlive()) {
                return;
            }
            {
                v8::Locker locker(_isolate);
                v8::Isolate::Scope isolateScope(_isolate);
                v8::HandleScope handleScope(_isolate);
                v8::Local<v8::Context> owner = _context->Get(_isolate);
                v8::Context::Scope contextScope(owner);
                _resolver->Get(_isolate)->Resolve(owner, build(_isolate, owner)).Check();
            }
            Release();
        }

        // Drops the promise without settling it. The caller holds the owner's scopes, or the owner is gone.
        void Release() {
            if (_resolver == nullptr) {
                return;
            }
            // A stopped resource's isolate is disposed: its handles cannot be released into it, so they are left behind.
            if (OwnerAlive()) {
                v8::Locker locker(_isolate);
                delete _resolver;
                delete _context;
            }
            _resolver = nullptr;
            _context  = nullptr;
        }

      private:
        bool OwnerAlive() const {
            auto *server  = Server::Get();
            auto *module  = server != nullptr ? server->GetScriptingModule() : nullptr;
            auto *manager = module != nullptr ? module->GetResourceManager() : nullptr;
            auto *calls   = manager != nullptr ? manager->GetFunctionReferences() : nullptr;
            return calls != nullptr && calls->FindRuntime(_isolate) != 0;
        }

        v8::Isolate *_isolate;
        v8::Global<v8::Promise::Resolver> *_resolver;
        v8::Global<v8::Context> *_context;
    };

    // Emits one reserved event to every loaded resource. `buildArgs(isolate)` runs inside the isolate and returns the argument vector, so a handle is only ever built once there is a resource manager to emit into; with scripting absent nothing runs at all.
    template <typename BuildArgs>
    void EmitReservedEvent(const char *eventName, BuildArgs &&buildArgs) {
        WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            std::vector<v8::Local<v8::Value>> args = buildArgs(isolate);
            resources->GetEvents().EmitReserved(isolate, context, eventName, args);
        });
    }

    // Emits one reserved event a handler may refuse, and says whether nobody did. Every handler runs and has returned before this does; one returning literal `false` refuses, and a handler that is async cannot. With scripting absent nothing is asked, which is not a refusal.
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
