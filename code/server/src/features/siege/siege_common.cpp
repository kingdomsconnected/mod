#include "siege_common.h"

#include "features/npc/npc_verbs.h"

#include "shared/features/npc/npc_entity.h"
#include "shared/features/siege/siege_ballistics.h"

#include <utility>

namespace KCDC::Features::Siege {
    void HarmNpcsInBlast(MafiaNet::VirtualWorldId virtualWorld, const glm::vec3 &point, float damage, float radius, std::uint64_t attackerNetworkId) {
        if (!(damage > 0.0F) || !(radius > 0.0F)) {
            return;
        }
        std::vector<std::pair<std::uint64_t, float>> struck;
        for (Shared::Entities::NpcEntity *npc : Npc::Npcs::Collect(virtualWorld)) {
            if (npc == nullptr || !npc->alive) {
                continue;
            }
            const float share = Shared::Game::Siege::DamageFalloff(glm::distance(npc->position, point), radius);
            if (share > 0.0F) {
                struck.emplace_back(npc->GetNetworkID(), damage * share);
            }
        }
        for (const std::pair<std::uint64_t, float> &hit : struck) {
            if (Shared::Entities::NpcEntity *npc = Npc::Npcs::Find(hit.first)) {
                Npc::Npcs::Damage(npc, attackerNetworkId, hit.second);
            }
        }
    }
} // namespace KCDC::Features::Siege
