#pragma once

#include <mafianet/types.h>
#include <networking/replication/network_entity.h>
#include <optional>

namespace KCDC::Shared::Entities {
    /**
     * Whether an entity belongs to the virtual world a query named, where no
     * world named means every one of them.
     *
     * Takes the base rather than each feature's own entity type: the virtual
     * world lives on `NetworkEntity`, and every server-side `Collect` asks this
     * the same way.
     */
    inline bool InWorld(const Framework::Networking::Replication::NetworkEntity *entity, const std::optional<MafiaNet::VirtualWorldId> &virtualWorld) {
        return entity != nullptr && (!virtualWorld.has_value() || entity->GetVirtualWorld() == *virtualWorld);
    }
} // namespace KCDC::Shared::Entities
