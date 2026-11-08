#pragma once

#include <unordered_map>

#include "shared/features/door/gate_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for one castle gate -- a drawbridge, a portcullis or a pair of swapped leaves. Wraps the gate's NetworkID and resolves the live entity through replication. Position comes from the framework's Entity base.
    //
    // Like a door, a gate is the level's own and is never spawned: the server builds every one from the compiled gate catalog, identified by the level's EntityGuid, and never takes a client's word about one. Its position comes from that catalog too, so `Gate.nearest` needs no
    // round trip to anybody's client.
    //
    // Bindings only: the cycle goes through KCDC::Features::Door::GateService, which owns the server clock every client plays the motion out from.
    class Gate final: public Framework::Scripting::Builtins::Entity {
      public:
        Gate(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live gate, or nullptr once it has gone.
        Shared::Entities::GateEntity *ResolveGate() const;

        // The level's own EntityGuid, as sixteen hex digits.
        std::string GetGuid() const;

        // `drawbridge`, `portcullis` or `gate`, from the gate's catalog row.
        std::string GetKind() const;

        // `closed`, `opening`, `open` or `closing`. Named for the cycle rather than the state, because the Entity base already has a `state` and it is the entity's state bag.
        std::string GetCycle() const;

        // 0 shut to 1 fully open, walked across while a cycle runs.
        float GetOpenness() const;

        bool IsMoving() const;

        // How long each direction takes, in seconds, from the key range the asset's DBA stores.
        float GetOpenDuration() const;
        float GetCloseDuration() const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Gate> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Gate>>> _classes;
    };
} // namespace KCDC::Scripting
