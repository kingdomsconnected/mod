#pragma once

#include "shared/features/ground_item/ground_item_definition.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/item_stack.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::GroundItem {
    // Everything the server does to a replicated ground stack. One place per verb, so the `/drop` command, the drop RPC handler and the scripting binding are three callers of the same code -- and so the event a verb raises cannot be raised by one caller and forgotten by the
    // next.
    //
    // A stack the server authors and a stack a player dropped differ in exactly two things: where it starts, and whether it is still falling. Everything else -- the class, the streaming range, the virtual world, the event -- is seeded here for both.
    //
    // Every query takes the virtual world it is about, because a stack belongs to one and the interest grid already refuses to stream it anywhere else. An empty optional means all of them, which is what a script gets when it names none.
    //
    // No V8. What happened goes through this feature's own KCDC::Scripting::Events, declared in ground_item_scripting.h, so a server with no resources loaded runs all of this unchanged.
    class GroundItems final {
      public:
        // Spawns and replicates one stack, already at rest.
        //
        // Born resting with its final pose. Raises `groundItemSpawn`.
        //
        // Null for a class no receiver could build the stack from, an amount of zero, or a position or rotation that is not finite. `item` is taken by value and completed in place -- see `GroundStock::ApplyClassDefaults`.
        // The stack, or null when it could not be built or a groundItemSpawn handler took it straight away. `name` is set before the handlers see it.
        static Shared::Entities::GroundItemEntity *Spawn(Shared::RPC::InventoryItem item, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, bool carryable, const std::string &name = {});

        // Lays down a stack a World Builder export placed: the class resolved as `GroundItem.spawn` resolves it, already at rest, named. Null, with `error` saying why, for a class the server does not know or a stack no client could build.
        static Shared::Entities::GroundItemEntity *Place(const Shared::Game::ItemPlacementDefinition &placement, std::uint32_t virtualWorld, std::string &error);

        // Spawns what a player's inventory gave up in a drop, in front of them and falling. The nearest client simulates it until it settles, then the server keeps it. Null for a stack no receiver could build. Raises `groundItemSpawn`.
        static Shared::Entities::GroundItemEntity *Drop(Shared::RPC::InventoryItem item, Shared::Entities::HumanEntity &source, bool retiresHeldItem, bool carryable);

        // Despawns it everywhere, raising `groundItemDestroy` while the entity can still be read.
        static void Destroy(Shared::Entities::GroundItemEntity *groundItem);

        // Removes every stack in one virtual world, raising `groundItemDestroy` for each. Returns how many went.
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        static std::size_t Count(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // Every live stack in one virtual world, in no particular order.
        static std::vector<Shared::Entities::GroundItemEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // One line per stack in one virtual world, newest first, for `/drop list`.
        static std::vector<std::string> Describe(std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::size_t limit);
    };
} // namespace KCDC::Features::GroundItem
