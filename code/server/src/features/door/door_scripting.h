#pragma once

#include <unordered_map>

#include "shared/features/door/door_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Script handle for a replicated door, resolved through replication so a dead one reads as gone.
    //
    // Most doors are the level's own: the server builds every one from the compiled door catalog and never takes a client's word that a door exists; `queryNearest` only asks which of them a player is standing at. A script or a world resource can also hang one of its own
    // (`Door.spawn`), and take a level door out of a world (`destroy`, `Door.remove`).
    class Door final: public Framework::Scripting::Builtins::Entity {
      public:
        Door(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::DoorEntity *ResolveDoor() const;

        std::string GetGuid() const;

        std::uint32_t GetStateToken() const;

        bool IsOpen() const;
        bool IsOpenedFromFront() const;

        bool IsLocked() const;
        void SetLocked(bool locked);
        bool IsLockedByServer() const;

        bool IsUsable() const;
        void SetUsable(bool usable);

        std::string GetName() const;
        std::string GetKeyItem() const;
        bool IsLockpickable() const;
        bool StartsLocked() const;

        // Whether a script or a world resource hung this door, rather than the level.
        bool IsSpawned() const;
        // The leaf mesh it hangs, from `Door.models()`.
        std::string GetModel() const;

        // The level areas that link this door as one of their `crime_door`s.
        v8::Local<v8::Value> GetAreas(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Door> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Door>>> _classes;
    };

    // What this feature tells loaded resources doors did. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        // A player's client reported working a door, and the game's own rules
        // allow it. Asked before the change is applied: false when a handler
        // refused it, and then the door is put back on every client.
        bool DoorInteract(std::uint64_t playerNetworkId, std::uint64_t doorNetworkId, const char *action, bool keySide);
    } // namespace Events
} // namespace KCDC::Scripting
