#pragma once

#include "shared/features/cart/cart_entity.h"
#include "shared/features/cart/cart_seat.h"
#include "shared/game/cart_blueprints.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace KCDC::Features::Cart {
    // Everything the server does to a cart. One place per verb, so the seat
    // RPC handler, the `/cart` command and the scripting binding are callers
    // of the same code -- and the event a verb raises cannot be raised by one
    // caller and forgotten by the next.
    //
    // No V8 here. What happened goes through this feature's own
    // KCDC::Scripting::Events, declared in cart_scripting.h. Script handlers
    // run synchronously inside the verbs and may destroy the cart a verb is
    // holding, so a verb looks it up again by ID after raising one.
    class Carts final {
      public:
        // Spawns and replicates one, standing at `position` and facing along
        // `rotation`, its lead point and its body in line. Raises
        // `cartSpawn`. Null for a pose that is not finite, or when a
        // `cartSpawn` handler destroyed it again.
        static Shared::Entities::CartEntity *Spawn(const Shared::Game::CartBlueprint &blueprint, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld);

        // Despawns it everywhere: everyone in it is let out first, each with a
        // `cartExit`, then `cartDestroy` is raised while it can still be read.
        // False when a destroy of the same cart is already under way.
        static bool Destroy(Shared::Entities::CartEntity *cart);

        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // Every live cart in one virtual world, or all of them.
        static std::vector<Shared::Entities::CartEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // The live cart with that network ID, or null.
        static Shared::Entities::CartEntity *Find(std::uint64_t cartNetworkId);

        // The cart `playerNetworkId` sits in, and which seat, or null.
        static Shared::Entities::CartEntity *Occupied(std::uint64_t playerNetworkId, std::size_t &seat);

        // Puts a player in one of the cart's seats. The one place a seat
        // is written: the player is taken out of whatever cart they were in
        // first, the reins pin the cart to the driver's client, and the forced
        // state tells that client. False, and nothing written, for a seat a
        // player cannot take, a taken one, a player that is not connected, a
        // cart being destroyed, or a `cartEntering` handler's refusal. Raises
        // `cartEnter`.
        static bool Seat(Shared::Entities::CartEntity *cart, std::uint8_t seat, std::uint64_t playerNetworkId);

        // Records how far the player in `seat` has got with their own game's
        // climb -- `Boarding`, `Seated` or `Leaving` -- which every other
        // client's copy of them follows. The caller has checked who sits there.
        static void MarkStage(Shared::Entities::CartEntity *cart, std::uint8_t seat, Shared::RPC::CartSeatRequest::Kind stage);

        // Takes a player out of whatever cart they are in, raising `cartExit`.
        // Their own client climbs down when it sees the seat go. False when
        // they were in none.
        static bool Unseat(std::uint64_t playerNetworkId);

        // Moves a cart somewhere else outright, standing, its body in line
        // with its lead; the forced state puts the driver's copy there too, and
        // they carry on from it.
        static bool Teleport(Shared::Entities::CartEntity *cart, const glm::vec3 &position, const glm::quat &rotation);

        // How fast the driver may take it urged on, 0 to `kCartMaxSpeed`; the
        // forced state tells the driver at once. False for a speed outside
        // that.
        static bool SetMaxSpeed(Shared::Entities::CartEntity *cart, float maxSpeed);
    };
} // namespace KCDC::Features::Cart
