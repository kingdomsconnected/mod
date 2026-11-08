#pragma once

#include "shared/features/dog/dog_entity.h"

#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Features::Dog {
    // Everything the server does to a dog. One place per verb, so the scripting binding, the disconnect sweep and anything else are callers of the same code -- and so the event a verb raises cannot be raised by one caller and forgotten by the next.
    //
    // No V8 here. What happened is reported through this feature's own KCDC::Scripting::Events, declared in dog_scripting.h, which is a notification seam rather than a binding: a server with no resources loaded runs all of this unchanged.
    class Dogs final {
      public:
        // Spawns and replicates one, already belonging to `ownerNetworkId`. Empty `soulGuid` takes the generic dog. Raises `dogSpawn`.
        static Shared::Entities::DogEntity *Spawn(const std::string &soulGuid, std::uint64_t ownerNetworkId, const glm::vec3 &position, const glm::quat &rotation, const std::string &name = std::string());

        // Despawns it everywhere, raising `dogDestroy` while the body can still be read.
        static void Destroy(Shared::Entities::DogEntity *dog);

        // The one place the name is written.
        static void Rename(Shared::Entities::DogEntity *dog, const std::string &name);

        // Hands the dog to another player's client, or back to the server when `playerNetworkId` is 0. Every client re-possesses the body onto the new owner's soul from the replicated field. Raises `dogOwnerChanged`. False, and nothing written, when the id names no player.
        static bool SetOwner(Shared::Entities::DogEntity *dog, std::uint64_t playerNetworkId);

        // Writes the companion mode every client applies. Raises `dogModeChanged` only on an actual change.
        static void SetMode(Shared::Entities::DogEntity *dog, std::uint8_t mode);

        // The dog belonging to this player, or nullptr. One dog per player, as in the game: the companion manager holds a single primary companion per category.
        static Shared::Entities::DogEntity *OwnedBy(std::uint64_t playerNetworkId);

        // What a disconnect does to the player's dogs: destroys each, since a masterless dog goes Forsaken in the game's own terms and NPCs are authored to notice -- unless a script cleared its `destroyWithOwner`, in which case it stays in the world with no master.
        static void OwnerLeft(std::uint64_t playerNetworkId);
    };
} // namespace KCDC::Features::Dog
