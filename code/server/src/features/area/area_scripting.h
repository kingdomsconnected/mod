#pragma once

#include <unordered_map>

#include "area_service.h"

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    // Script handle for one area: one of the level's own, or one a script built. Not an entity -- nothing is replicated -- so it holds the service's handle, which is never reused: a destroyed area's handle reads as gone rather than as whatever took its place.
    class Area final {
      public:
        explicit Area(std::uint32_t handle): _handle(handle) {}

        KCDC::Features::Area::AreaRecord *Resolve() const;

        std::uint32_t GetHandle() const {
            return _handle;
        }

        bool Exists() const;
        std::string GetId() const;
        std::string GetKind() const;
        std::string GetType() const;

        std::string GetName() const;
        void SetName(const std::string &name);

        std::vector<std::string> GetLabels() const;
        void SetLabels(const std::vector<std::string> &labels);

        v8::Local<v8::Value> GetMetadata(v8::Isolate *isolate) const;
        void SetMetadata(v8::Isolate *isolate, v8::Local<v8::Value> value);

        bool HasCollision() const;
        void SetCollision(v8::Isolate *isolate, v8::Local<v8::Value> value);
        bool HasCollisionFloor() const;
        void SetCollisionFloor(v8::Isolate *isolate, v8::Local<v8::Value> value);
        bool HasCollisionCeiling() const;
        void SetCollisionCeiling(v8::Isolate *isolate, v8::Local<v8::Value> value);
        double GetCollisionThickness() const;
        void SetCollisionThickness(v8::Isolate *isolate, v8::Local<v8::Value> value);
        bool IsEnabled() const;
        void SetEnabled(bool enabled);

        v8::Local<v8::Value> GetVirtualWorld(v8::Isolate *isolate) const;
        void SetVirtualWorld(v8::Isolate *isolate, v8::Local<v8::Value> value);

        v8::Local<v8::Value> GetPosition(v8::Isolate *isolate) const;
        void SetPosition(v8::Isolate *isolate, v8::Local<v8::Value> value);

        v8::Local<v8::Value> GetRotation(v8::Isolate *isolate) const;
        void SetRotation(v8::Isolate *isolate, v8::Local<v8::Value> value);

        float GetHeight() const;
        std::string GetLocation() const;

        v8::Local<v8::Value> GetBounds(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetMembers(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetDoors(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetPlayers(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetOccupants(v8::Isolate *isolate) const;

        std::string ToString() const;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Area> &GetClass(v8::Isolate *isolate);

      private:
        KCDC::Shared::Game::AreaCollisionOptions GetCollisionOptions() const;
        void ApplyCollisionOptions(v8::Isolate *isolate, const KCDC::Shared::Game::AreaCollisionOptions &options);

        std::uint32_t _handle = 0;

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Area>>> _classes;
    };

    // Turns a service handle into the handle a script sees; null for one that has gone.
    v8::Local<v8::Value> WrapArea(v8::Isolate *isolate, std::uint32_t handle);

    // The objects `Door.getForArea` answers with, one per `crime_door` link.
    v8::Local<v8::Value> AreaDoorsToArray(v8::Isolate *isolate, const std::vector<KCDC::Features::Area::AreaDoor> &doors);

    // An array of area handles.
    v8::Local<v8::Value> AreasToArray(v8::Isolate *isolate, const std::vector<KCDC::Features::Area::AreaRecord *> &areas);

    namespace Events {
        // A body crossed an area's boundary. Raised from the server tick.
        void AreaCrossed(bool enter, std::uint32_t areaHandle, std::uint64_t networkId, KCDC::Features::Area::AreaOccupantKind kind, bool matchingVirtualWorld);
    } // namespace Events
} // namespace KCDC::Scripting
