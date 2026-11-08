#pragma once

#include "features/npc/npc_order.h"
#include "shared/features/horse/horse_entity.h"

namespace KCDC::Features::Horse {
    /** Horse orders use the same route state machine and query budget as NPCs. */
    class HorseNavigation final {
      public:
        static HorseNavigation &Get();
        bool MoveTo(Shared::Entities::HorseEntity *horse, const glm::vec3 &point, Shared::Game::NpcSpeed speed, float radius);
        bool Patrol(Shared::Entities::HorseEntity *horse, const std::vector<glm::vec3> &points, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds);
        void Hold(Shared::Entities::HorseEntity *horse);
        void Forget(std::uint64_t id);
        void Update();
        const char *Status(Shared::Entities::HorseEntity *horse);

      private:
        static Npc::NpcOrderContext Context(Shared::Entities::HorseEntity *horse);
        static void Publish(Shared::Entities::HorseEntity *horse, Npc::NpcOrder &order);
        void UpdateHorse(std::uint64_t id);
        Npc::NpcOrders _orders;
        std::unordered_map<std::uint64_t, MafiaNet::PeerGuid> _simulators;
        std::vector<std::uint64_t> _updateIds;
    };
} // namespace KCDC::Features::Horse
