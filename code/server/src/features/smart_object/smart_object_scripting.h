#pragma once

#include "shared/features/smart_object/smart_object_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace KCDC::Scripting {
    // Script handle for a smart object a script or a world resource placed: a bench, a bed, a dice table, an alchemy table, a smithy, a drying rack or a smokehouse every player can use as the level's own.
    //
    // The level's own stations have no handle: `SmartObject.stations` lists them by GUID, and `SmartObject.remove` takes one out.
    class SmartObject final: public Framework::Scripting::Builtins::Entity {
      public:
        SmartObject(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::SmartObjectEntity *ResolveObject() const;

        std::string GetBlueprint() const;
        std::string GetKind() const;
        std::string GetName() const;
        // The station part's GUID, as `Dice.tables()` and `Crafting.stations()` name it; empty for a seat or a bed.
        std::string GetStationGuid() const;
        // The GUIDs of the parts a body sits or lies on, as a seated player's stance names them.
        v8::Local<v8::Value> GetSeats(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<SmartObject> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<SmartObject>>> _classes;
    };

    v8::Local<v8::Value> WrapSmartObject(v8::Isolate *isolate, std::uint64_t networkId);
} // namespace KCDC::Scripting
