#pragma once

#include <unordered_map>

#include "shared/features/marker/marker_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated marker. Wraps the NetworkID and resolves through replication, so a handle to a removed marker reads as gone rather than dangling.
    //
    // Bindings only: every verb is one call into KCDC::Features::Marker::Markers, so a script and a command seed a marker the same way.
    class Marker final: public Framework::Scripting::Builtins::Entity {
      public:
        Marker(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live marker, or nullptr once it has been removed.
        Shared::Entities::MarkerEntity *ResolveMarker() const;

        // Empty when this build's catalog does not have the marker's row.
        std::string GetMaterial() const;
        std::string GetGroup() const;

        // Read-only: changing the shape is a rebuild, not an assignment.
        std::string GetShape() const;

        // None of these rebuilds: extents are restated in place, motion plays on each client's own clock.
        float GetSize() const;
        void SetSize(float size);
        float GetHeight() const;
        void SetHeight(float height);
        float GetSpin() const;
        void SetSpin(float spin);
        float GetBob() const;
        void SetBob(float bob);

        // Packed 0xRRGGBBAA tint, or 0 for the material as it ships.
        double GetColor() const;
        void SetColor(double color);

        // Whether clients watch the local player against this marker and report crossings.
        bool GetTrigger() const;
        void SetTrigger(bool trigger);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Marker> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Marker>>> _classes;
    };

    // Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void MarkerPlaced(std::uint64_t markerNetworkId);

        // A player crossed a trigger marker's boundary, once the server has
        // agreed they are where the client says they are.
        void MarkerEntered(std::uint64_t markerNetworkId, std::uint64_t playerNetworkId);
        void MarkerExited(std::uint64_t markerNetworkId, std::uint64_t playerNetworkId);

        // Raised while the marker is still resolvable, so a handler can read what it was and where.
        void MarkerRemoved(std::uint64_t markerNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
