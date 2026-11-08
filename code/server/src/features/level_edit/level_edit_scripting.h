#pragma once

#include <unordered_map>

#include "shared/features/level_edit/level_edit_entity.h"

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Script handle for one server edit to an object the level placed. Not an entity handle: an edit has no pose of its own to assign -- its key is fixed and what it does to the object is its whole state -- so it holds the edit's NetworkID and resolves it through replication, reading as gone once the edit is restored.
    //
    // Bindings only: every verb is one call into KCDC::Features::LevelEdit::LevelEdits.
    class LevelEdit final {
      public:
        explicit LevelEdit(std::uint64_t networkId): _networkId(networkId) {}

        // The live edit, or nullptr once it has been restored.
        Shared::Entities::LevelEditEntity *Resolve() const;

        double GetId() const {
            return static_cast<double>(_networkId);
        }

        bool Exists() const;
        std::string GetKind() const;
        std::string GetName() const;
        std::string GetGuid() const;
        double GetVirtualWorld() const;

        bool IsHidden() const;
        void SetHidden(bool hidden);

        std::string GetModel() const;
        // Throws for a mesh no client could load, which v8pp turns into a script exception.
        void SetModel(const std::string &model);

        bool IsMoved() const;

        std::string ToString() const;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<LevelEdit> &GetClass(v8::Isolate *isolate);

      private:
        std::uint64_t _networkId = 0;

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<LevelEdit>>> _classes;
    };

    // The handle a script sees for an edit's NetworkID, or JS null once it is gone.
    v8::Local<v8::Value> WrapLevelEdit(v8::Isolate *isolate, std::uint64_t levelEditNetworkId);
} // namespace KCDC::Scripting
