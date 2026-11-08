#pragma once

#include <unordered_map>

#include "shared/features/cart/cart_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    // Server-side handle for a replicated cart. Wraps the cart's NetworkID and
    // resolves the live entity through replication, so a handle to a despawned
    // cart reads as gone rather than dangling.
    //
    // Bindings only: every verb is one call into KCDC::Features::Cart::Carts,
    // which the seat RPC handler and the `/cart` command also use.
    class Cart final: public Framework::Scripting::Builtins::Entity {
      public:
        Cart(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live cart, or nullptr once it has been despawned.
        Shared::Entities::CartEntity *ResolveCart() const;

        std::string GetBlueprint() const;

        // The number of horses in its shafts: 2 for a wagon, 1 for the two-
        // wheeler, 0 for the one chassis that cannot be pulled.
        int GetHorseCount() const;

        v8::Local<v8::Value> GetDriverHandle(v8::Isolate *isolate) const;

        // `stand`, `walk`, `trot` or `reverse`, as the driver last asked.
        std::string GetPace() const;

        // The fastest the driver may take it urged on, in metres a second.
        float GetMaxSpeed() const;
        void SetMaxSpeed(float maxSpeed);

        // The names of its seats, the reins first.
        std::vector<std::string> GetSeatNames() const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Cart> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Cart>>> _classes;
    };

    // The name scripts see for a seat of a cart; empty for a seat it does not
    // have, or a cart that is gone.
    std::string CartSeatName(std::uint64_t cartNetworkId, std::uint8_t seat);

    // What this feature tells loaded resources happened to a cart. Event
    // arguments are network IDs; the handles a resource sees are created inside
    // the scripting isolate.
    namespace Events {
        void CartSpawned(std::uint64_t cartNetworkId);

        // Raised while the cart is still resolvable, after everyone in it has
        // been let out.
        void CartDestroyed(std::uint64_t cartNetworkId);

        // Before a player is put in a seat; any handler returning false refuses
        // it.
        bool CartEntering(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat);

        void CartEntered(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat);
        void CartExited(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat);
    } // namespace Events
} // namespace KCDC::Scripting
