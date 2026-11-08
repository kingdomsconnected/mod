#pragma once

#include <unordered_map>

#include "world_builder_service.h"
#include <memory>
#include <v8.h>
#include <v8pp/class.hpp>

namespace KCDC::Scripting {
    /** The `WorldResource` global: a handle onto one export `WorldBuilderService` loaded, resolved by handle on every read so it never dangles. */
    class WorldResource final {
      public:
        explicit WorldResource(std::uint32_t handle): _handle(handle) {}
        const KCDC::Features::WorldBuilder::WorldResourceRecord *Resolve() const;
        std::string GetName() const;
        std::string GetPath() const;
        std::string GetLevel() const;
        bool IsLoaded() const;
        v8::Local<v8::Value> GetProps(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetEffects(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetLights(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetLevelEdits(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetDoors(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetSmartObjects(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetItems(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetAreas(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetPatrolRoutes(v8::Isolate *isolate) const;
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        std::uint32_t _handle;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<WorldResource>>> _classes;
    };
} // namespace KCDC::Scripting
