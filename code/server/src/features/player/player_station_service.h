#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_station.h"

#include <unordered_map>

namespace KCDC::Features::Player {
    class StationService final {
      public:
        static StationService &Get();
        void Install();
        void Update();
        void Forget(std::uint64_t player);

      private:
        struct Record {
            Shared::RPC::PlayerStation report;
            std::uint32_t world         = 0;
            std::uint64_t expires       = 0;
            std::uint32_t sceneRevision = 0;
            std::uint64_t sceneAt       = 0;
        };
        void Receive(const Shared::RPC::PlayerStation &report, MafiaNet::PeerGuid peer);
        void ReceiveScene(const Shared::RPC::DiceScene &report, MafiaNet::PeerGuid peer);
        bool Allowed(Shared::Entities::HumanEntity &human, const Shared::Game::StationState &state) const;
        std::unordered_map<std::uint64_t, Record> _records;
    };
} // namespace KCDC::Features::Player
