#pragma once

#include <unordered_map>

#include "features/stash/stash_contents.h"
#include "shared/features/stash/stash_entity.h"

#include <scripting/builtins/entity.h>

#include <glm/vec3.hpp>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated container. Wraps the stash's NetworkID and resolves the live entity through replication, so a handle to a despawned stash reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base.
    //
    // A container is either spawned by a script or placed by the level. The level's are never spawned: the server builds every one from the compiled stash catalog, and `Stash.find` / `Stash.nearest` address them.
    //
    // Bindings only: every verb is one call into KCDC::Features::Stash::StashService, which owns the contents, the view that makes one player's looting exclusive, and the gate every open, unlock and lockpick passes.
    class Stash final: public Framework::Scripting::Builtins::Entity {
      public:
        Stash(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live stash, or nullptr once it has been despawned.
        Shared::Entities::StashEntity *ResolveStash() const;

        // What every client turns into the same native EntityGuid. Minted by the server, from 1; 0 for a level container.
        std::uint32_t GetStashId() const;

        // The EntityGuid every client finds it under, level or spawned, as hex.
        std::string GetGuid() const;

        // The level's EntityGuid as hex, or empty for a spawned container.
        std::string GetLevelGuid() const;

        std::string GetName() const;

        // How many rows the server is holding for this container.
        std::uint32_t GetItemCount() const;

        // The body that has it open, or 0 when nobody has.
        std::uint64_t GetHolderId() const;

        // Whether a player has it open, which is what every client animates the lid by.
        bool IsOpen() const;
        bool IsVirtual() const;

        bool IsLocked() const;
        void SetLocked(bool locked);
        bool IsLockedByServer() const;

        bool IsLockpickable() const;
        void SetLockpickable(bool lockpickable);

        v8::Local<v8::Value> GetLockDifficulty(v8::Isolate *isolate) const;
        void SetLockDifficulty(v8::Isolate *isolate, v8::Local<v8::Value> value);

        v8::Local<v8::Value> GetLockFanciness(v8::Isolate *isolate) const;
        void SetLockFanciness(v8::Isolate *isolate, v8::Local<v8::Value> value);

        std::string GetModel() const;

        std::string GetKeyItem() const;
        void SetKeyItem(std::string keyItem);

        bool StartsLocked() const;

        // The horizontal direction a player must face the container along to be offered it.
        glm::vec3 GetUseDirection() const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Stash> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Stash>>> _classes;
    };

    // What this feature tells loaded resources containers did. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        // A player pressed a container's prompt, and the game's own rules
        // allow it. Asked before anything happens: false when a handler
        // refused it, and then the player's client never runs it.
        bool StashInteract(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId, const char *action);

        // A container now exists: one a script spawned, or one the level places, built in a virtual world.
        void StashSpawned(std::uint64_t stashNetworkId);

        // One of `stashOpen`, `stashClose`, `stashInventoryChanged` or `stashDestroy`, raised while the container can still be read.
        void StashChanged(const KCDC::Features::Stash::StashEvent &event);
    } // namespace Events
} // namespace KCDC::Scripting
