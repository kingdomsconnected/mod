#pragma once

#include <unordered_map>

#include "shared/features/vfx/vfx_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated particle effect. Wraps the effect's NetworkID and resolves the live entity through replication, so a handle to a stopped effect reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base.
    //
    // Bindings only: every verb is one call into KCDC::Features::Vfx::Effects, so an effect placed from a script and one placed from a command are seeded the same way and raise the same events.
    class Vfx final: public Framework::Scripting::Builtins::Entity {
      public:
        Vfx(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live effect, or nullptr once it has been stopped.
        Shared::Entities::VfxEntity *ResolveVfx() const;

        // Empty when this build's catalog does not have the effect's row.
        std::string GetEffect() const;
        std::string GetLibrary() const;
        std::string GetGroup() const;

        // Each of these is read when the emitter is created and never again, so assignment rebuilds the effect on every client that can see it.
        float GetScale() const;
        void SetScale(float scale);
        float GetCountScale() const;
        void SetCountScale(float countScale);
        float GetSpeedScale() const;
        void SetSpeedScale(float speedScale);
        float GetTimeScale() const;
        void SetTimeScale(float timeScale);
        float GetStrength() const;
        void SetStrength(float strength);
        float GetPulsePeriod() const;
        void SetPulsePeriod(float pulsePeriod);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Vfx> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vfx>>> _classes;
    };

    // What this feature tells loaded resources happened to a replicated effect. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void VfxSpawned(std::uint64_t vfxNetworkId);

        // Raised while the effect is still resolvable, so a handler can read what it was and where.
        void VfxDestroyed(std::uint64_t vfxNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
