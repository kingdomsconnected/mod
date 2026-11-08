#pragma once

#include <unordered_map>

#include "shared/features/ground_item/ground_item_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for one stack lying in the world. Wraps the stack's NetworkID and resolves the live entity through replication, so a handle to a stack somebody has picked up reads as gone rather than dangling. Transform and virtual world come from the framework's Entity
    // base.
    //
    // Bindings only: every verb is one call into KCDC::Features::GroundItem::GroundItems, which the drop RPC and the `/drop` command also use, so a stack a resource spawned and a stack a player threw down are seeded the same way and raise the same events.
    //
    // Everything about the stack itself is read-only. A receiver builds the native item from the class and its scalars once, when the entity is first streamed to it, and never reads them again -- so a setter would change what a client joining later sees while leaving everyone
    // already watching with the old stack. Spawn the stack you want.
    class GroundItem final: public Framework::Scripting::Builtins::Entity {
      public:
        GroundItem(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live stack, or nullptr once it has been picked up or despawned.
        Shared::Entities::GroundItemEntity *ResolveGroundItem() const;

        std::string GetItemClass() const;

        std::uint32_t GetAmount() const;
        std::uint32_t GetQuality() const;
        float GetHealth() const;
        float GetCondition() const;
        bool GetResting() const;
        bool GetCarryable() const;
        std::string GetName() const;
        std::uint64_t GetDroppedById() const;
        v8::Local<v8::Value> GetDroppedByHandle(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<GroundItem> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<GroundItem>>> _classes;
    };

    // What this feature tells loaded resources happened to a stack. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void GroundItemSpawned(std::uint64_t groundItemNetworkId);

        // Raised while the stack is still resolvable, so a handler can read its class and its pose.
        void GroundItemDestroyed(std::uint64_t groundItemNetworkId);

        // A player's pickup was granted and the stack is about to go. Raised before the despawn, which raises its own event after this one.
        void GroundItemPickedUp(std::uint64_t groundItemNetworkId, std::uint64_t playerNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
