#pragma once

#include <unordered_map>

#include "shared/features/dog/dog_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated dog. Wraps the dog's NetworkID and resolves the live entity through replication, so a handle to a destroyed dog reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base; what is added here is
    // the dog's identity, who it belongs to, and what it is doing.
    //
    // Bindings only: every verb is one call into KCDC::Features::Dog::Dogs, which the disconnect sweep also uses, so a dog spawned from a script and one spawned from anywhere else are seeded the same way and raise the same event.
    class Dog final: public Framework::Scripting::Builtins::Entity {
      public:
        Dog(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live dog, or nullptr once it has been despawned.
        Shared::Entities::DogEntity *ResolveDog() const;

        // The soul GUID the body was spawned against.
        std::string GetSoul() const;

        // The name every client shows, or empty when nobody has renamed it.
        std::string GetName() const;

        // Renames the dog on every client. Empty hands it back to its soul's own name.
        void SetName(const std::string &name);

        // NetworkID of the player this dog belongs to, 0 when it has no master.
        std::uint64_t GetOwnerId() const;

        // The owner as a handle, or JS null when the dog has no master.
        v8::Local<v8::Value> GetOwnerHandle(v8::Isolate *isolate) const;

        // Whether the dog is despawned when its owner disconnects, rather than left masterless.
        bool GetDestroyWithOwner() const;
        void SetDestroyWithOwner(bool destroy);

        // E_DogCompanionMode: 0 Wait, 1 Follow, 2 Free, 3 Aggressive, 4 Search, 5 Hunt, 6 Guard, 7 Ambush.
        std::uint32_t GetMode() const;
        void SetMode(std::uint32_t mode);

        // E_DogObjective of whatever the owner's game is running, or 25 (Invalid) when the dog is idle. Owner-reported, so read-only.
        std::uint32_t GetObjective() const;

        // The dog's morale as its owner last read it, or 0 before any report.
        float GetMorale() const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Dog> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Dog>>> _classes;
    };

    // What this feature tells loaded resources happened to a dog. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void DogSpawned(std::uint64_t dogNetworkId);
        void DogDestroyed(std::uint64_t dogNetworkId);
        void DogOwnerChanged(std::uint64_t dogNetworkId, std::uint64_t playerNetworkId);
        void DogModeChanged(std::uint64_t dogNetworkId, std::uint8_t mode);
    } // namespace Events
} // namespace KCDC::Scripting
