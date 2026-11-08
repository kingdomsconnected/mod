#pragma once

#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace KCDC::Features::Player {
    /**
     * One side of a blow or a shot: a player's body or a server NPC's, asked
     * the same questions. An NPC's life is its `spawnEpoch` and it can act
     * while alive; a player's is its soul's incarnation and state.
     */
    struct Combatant {
        Shared::Entities::HumanEntity *human                      = nullptr;
        Shared::Entities::NpcEntity *npc                          = nullptr;
        Framework::Networking::Replication::NetworkEntity *entity = nullptr;

        explicit operator bool() const {
            return entity != nullptr;
        }

        std::uint32_t Incarnation() const {
            return npc != nullptr ? npc->spawnEpoch : human->soul.incarnation;
        }

        /** Whether the body is in a life at all: a living NPC, or a player's valid soul. */
        bool Valid() const {
            return npc != nullptr ? npc->alive : human->soul.valid;
        }

        /** Whether the body can still act on a blow: a living NPC, or a player whose soul can. */
        bool CanAct() const {
            return npc != nullptr ? npc->alive : human->soul.CanAct();
        }

        bool Ready() const {
            return npc != nullptr ? npc->transformReady : human->transformReady;
        }

        glm::vec3 Position() const {
            return npc != nullptr ? npc->position : human->position;
        }

        static Combatant Find(std::uint64_t networkId) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (replication == nullptr || networkId == 0) {
                return {};
            }
            if (auto *npc = replication->GetEntity<Shared::Entities::NpcEntity>(networkId)) {
                return {nullptr, npc, npc};
            }
            if (auto *human = replication->GetEntity<Shared::Entities::HumanEntity>(networkId)) {
                return {human, nullptr, human};
            }
            return {};
        }

        /**
         * The body a connection speaks for: the NPC it names when that
         * connection simulates it, otherwise the connection's own player.
         */
        static Combatant Sender(std::uint64_t networkId, MafiaNet::PeerGuid guid) {
            const Combatant named = Find(networkId);
            if (named.npc != nullptr) {
                return named.entity->ownerGUID == guid ? named : Combatant {};
            }
            auto *replication = Framework::CoreModules::GetReplication();
            auto *human       = replication != nullptr ? replication->GetViewerAs<Shared::Entities::HumanEntity>(guid) : nullptr;
            return human != nullptr ? Combatant {human, nullptr, human} : Combatant {};
        }
    };
} // namespace KCDC::Features::Player
