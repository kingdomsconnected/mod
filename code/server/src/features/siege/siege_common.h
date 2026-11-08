#pragma once

#include "shared/entity_filter.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/virtual_world.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Siege {
    /** What a siege verb did, and the phrase that explains it when it did nothing; `SiegeResult` to scripts. */
    struct SiegeResult {
        bool accepted = false;

        /** Why nothing happened, as a phrase that reads after the thing's name: `is not loaded`. Empty when it did. */
        std::string reason;

        /** How long until the step this started is over, in seconds; for `fire`, until the projectile is let go. */
        float seconds = 0.0F;
    };

    /** Every replicated `Entity` in `virtualWorld`, or in every world for nothing. */
    template <typename Entity>
    std::vector<Entity *> CollectSiegeEntities(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Entity *> entities;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Entity>([&](Entity *entity) {
                if (Shared::Entities::InWorld(entity, virtualWorld)) {
                    entities.push_back(entity);
                }
            });
        }
        return entities;
    }

    /** The live `Entity` a network id names, or nullptr. */
    template <typename Entity>
    Entity *FindSiegeEntity(std::uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && networkId != 0 ? replication->GetEntity<Entity>(networkId) : nullptr;
    }

    /**
     * Takes a blast's share off every live NPC within `radius` of `point`.
     * An NPC's health is the server's ledger; a player's is its own client's.
     * Measured first and applied after, each resolved again: a handler of the
     * `npcDamage` this raises may despawn another of them.
     */
    void HarmNpcsInBlast(MafiaNet::VirtualWorldId virtualWorld, const glm::vec3 &point, float damage, float radius, std::uint64_t attackerNetworkId);
} // namespace KCDC::Features::Siege
