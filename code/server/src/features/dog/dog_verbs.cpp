#include "dog_verbs.h"

#include "features/dog/dog_scripting.h"

#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <vector>

namespace KCDC::Features::Dog {
    namespace {
        constexpr const char *kLogger = "KCDCServer";

        // The peer that plays this body, or the unassigned guid when the id names nothing live.
        MafiaNet::PeerGuid PeerOf(std::uint64_t playerNetworkId) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (replication == nullptr || playerNetworkId == 0) {
                return MafiaNet::UNASSIGNED_PEER_GUID;
            }

            auto *human = replication->GetEntity<Shared::Entities::HumanEntity>(playerNetworkId);
            return human != nullptr ? human->ownerGUID : MafiaNet::UNASSIGNED_PEER_GUID;
        }
    } // namespace

    Shared::Entities::DogEntity *Dogs::Spawn(const std::string &soulGuid, std::uint64_t ownerNetworkId, const glm::vec3 &position, const glm::quat &rotation, const std::string &name) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *dog = replication->CreateEntity<Shared::Entities::DogEntity>();
        if (dog == nullptr) {
            return nullptr;
        }

        dog->soulGuid = soulGuid.empty() ? Shared::Entities::DogEntity::kDefaultSoulGuid : soulGuid;

        // Before the entity replicates, so the name and the master ride the construction snapshot rather than arriving a tick after the body. A dog that spawned masterless for one tick would be a stray on every client for that tick, and the world is authored to notice one.
        dog->name           = name;
        dog->ownerNetworkId = ownerNetworkId;
        dog->position       = position;
        dog->rotation       = rotation;
        dog->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);

        Framework::Logging::GetLogger(kLogger)->info("Spawned dog {} (soul '{}', name '{}', owner {}) at {:.2f}, {:.2f}, {:.2f}", dog->GetNetworkID(), dog->soulGuid, dog->name, ownerNetworkId, position.x, position.y, position.z);

        // Authority last: SetOwner tells the new owner directly, and it has nothing to be told about until the fields above are settled.
        const MafiaNet::PeerGuid owner = PeerOf(ownerNetworkId);
        if (owner != MafiaNet::UNASSIGNED_PEER_GUID) {
            dog->SetOwner(owner);
        }

        Scripting::Events::DogSpawned(dog->GetNetworkID());
        return dog;
    }

    void Dogs::Destroy(Shared::Entities::DogEntity *dog) {
        if (dog == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can still read its owner and its name.
        Scripting::Events::DogDestroyed(dog->GetNetworkID());

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(dog);
        }
    }

    void Dogs::Rename(Shared::Entities::DogEntity *dog, const std::string &name) {
        if (dog == nullptr || dog->name == name) {
            return;
        }

        dog->name = name;

        // Nothing else to do: the key on the animal's soul is built from its NetworkID, so it is already right, and the localization hook reads this field back every time the engine resolves that key.
        Framework::Logging::GetLogger(kLogger)->info("Dog {} renamed to '{}'", dog->GetNetworkID(), name);
    }

    bool Dogs::SetOwner(Shared::Entities::DogEntity *dog, std::uint64_t playerNetworkId) {
        const MafiaNet::PeerGuid owner = PeerOf(playerNetworkId);
        if (dog == nullptr || (playerNetworkId != 0 && owner == MafiaNet::UNASSIGNED_PEER_GUID)) {
            return false;
        }
        if (dog->ownerNetworkId == playerNetworkId) {
            return true;
        }

        const std::uint64_t previous = dog->ownerNetworkId;
        dog->ownerNetworkId          = playerNetworkId;

        if (owner == MafiaNet::UNASSIGNED_PEER_GUID) {
            // Back to the server, which then places the animal for everyone from its own last known pose. Every client releases the possession from the replicated field on its next frame.
            dog->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
        }
        else {
            // Authority first, then the push: ForceState only speaks to an owner, and it is what fences the new owner's in-flight updates -- which still carry the previous master -- from clearing what was just set.
            dog->SetOwner(owner);
            dog->ForceState();
        }

        Framework::Logging::GetLogger(kLogger)->info("Dog {} handed from player {} to player {}", dog->GetNetworkID(), previous, playerNetworkId);
        Scripting::Events::DogOwnerChanged(dog->GetNetworkID(), playerNetworkId);
        return true;
    }

    void Dogs::SetMode(Shared::Entities::DogEntity *dog, std::uint8_t mode) {
        if (dog == nullptr || dog->mode == mode) {
            return;
        }

        dog->mode = mode;

        // Forced as well as replicated: the field channel is withheld from the dog's own owner, which is the one peer that has to act on a mode change.
        dog->ForceState();

        Framework::Logging::GetLogger(kLogger)->info("Dog {} switched to mode {}", dog->GetNetworkID(), mode);
        Scripting::Events::DogModeChanged(dog->GetNetworkID(), mode);
    }

    Shared::Entities::DogEntity *Dogs::OwnedBy(std::uint64_t playerNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || playerNetworkId == 0) {
            return nullptr;
        }

        Shared::Entities::DogEntity *owned = nullptr;
        replication->ForEach<Shared::Entities::DogEntity>([&owned, playerNetworkId](Shared::Entities::DogEntity *dog) {
            if (dog->ownerNetworkId == playerNetworkId) {
                owned = dog;
            }
        });
        return owned;
    }

    void Dogs::OwnerLeft(std::uint64_t playerNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || playerNetworkId == 0) {
            return;
        }

        // Every one of them, not just `OwnedBy`'s: nothing stops a script giving a player a second dog. Collected first, since destroying an entity from inside the walk would pull it out from under the iteration.
        std::vector<Shared::Entities::DogEntity *> owned;
        replication->ForEach<Shared::Entities::DogEntity>([&owned, playerNetworkId](Shared::Entities::DogEntity *dog) {
            if (dog->ownerNetworkId == playerNetworkId) {
                owned.push_back(dog);
            }
        });

        for (Shared::Entities::DogEntity *dog : owned) {
            if (dog->destroyWithOwner) {
                Destroy(dog);
            }
            else {
                // Back to the server, which has no brain to run it: the body stays where the owner's game last put it until a script hands it to somebody.
                SetOwner(dog, 0);
            }
        }
    }
} // namespace KCDC::Features::Dog
