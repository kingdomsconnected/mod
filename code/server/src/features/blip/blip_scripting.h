#pragma once

#include <unordered_map>

#include "shared/features/blip/blip_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated blip. Wraps the NetworkID and resolves through replication, so a handle to a removed blip reads as gone rather than dangling.
    //
    // Bindings only: every verb is one call into KCDC::Features::Blip::Blips.
    class Blip final: public Framework::Scripting::Builtins::Entity {
      public:
        Blip(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live blip, or nullptr once it has been removed.
        Shared::Entities::BlipEntity *ResolveBlip() const;

        std::string GetType() const;
        void SetType(std::string type);

        double GetMarkState() const;
        void SetMarkState(double state);

        std::string GetLabel() const;
        void SetLabel(std::string label);

        bool GetCompass() const;
        void SetCompass(bool compass);

        bool GetMap() const;
        void SetMap(bool map);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Blip> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Blip>>> _classes;
    };
} // namespace KCDC::Scripting
