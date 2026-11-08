#pragma once

#include "patrol_routes.h"
#include <memory>
#include <v8.h>
#include <v8pp/class.hpp>

namespace KCDC::Scripting {
    class PatrolRoute final {
      public:
        explicit PatrolRoute(std::uint32_t handle): _handle(handle) {}
        const Shared::Game::PatrolRouteDefinition *Resolve() const {
            return Features::Patrol::PatrolRoutes::Get().Find(_handle);
        }
        bool Exists() const {
            return Resolve() != nullptr;
        }
        std::string Id() const {
            const auto *route = Resolve();
            return route ? route->id : std::string {};
        }
        std::string Name() const {
            const auto *route = Resolve();
            return route ? route->name : std::string {};
        }
        std::uint32_t Handle() const {
            return _handle;
        }
        bool Destroy() {
            return Features::Patrol::PatrolRoutes::Get().Destroy(_handle);
        }
        v8::Local<v8::Value> ToJSON(v8::Isolate *isolate) const;
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::uint32_t _handle;
    };

    v8::Local<v8::Value> WrapPatrolRoute(v8::Isolate *isolate, std::uint32_t handle);
    bool ReadPatrolRoute(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::PatrolRouteDefinition &route, std::string &error);
    bool ApplyPatrolOptions(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::PatrolRouteDefinition &route, std::string &error);
    void EmitPatrolEvents(std::uint64_t networkId, std::vector<nlohmann::json> events);
} // namespace KCDC::Scripting
