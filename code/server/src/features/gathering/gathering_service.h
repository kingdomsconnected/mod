#pragma once

#include "features/gathering/gathering_module.h"

#include <mafianet/types.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace KCDC::Features::Gathering {
    /** Herb picking between the owner's game and the in-memory `GatheringModule`. */
    class GatheringService final {
      public:
        GatheringService();

        void Install(const std::string &level);
        void Update();
        void Reset();
        void OnPlayerDisconnect(MafiaNet::PeerGuid peer);
        bool SetRespawnTime(double minHours, double maxHours);
        void ResetRespawnTime();
        const std::optional<GatheringRespawnTime> &GetRespawnTime() const {
            return _gathering.GetRespawnTime();
        }
        std::size_t Respawn(std::uint32_t virtualWorld, const std::optional<glm::vec3> &position, float radius);
        std::vector<KCDC::Shared::Game::GatheringSource> GetHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) const;
        std::size_t RemoveHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld);
        std::size_t RespawnHerbs(const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld);

      private:
        struct Delivery {
            std::uint32_t virtualWorld = 0;
            std::shared_ptr<const std::vector<GatheringPoint>> points;
            nlohmann::json payload;
            std::size_t offset           = 0;
            bool respawn                 = false;
            bool full                    = false;
            std::uint64_t syncGeneration = 0;
        };

        void Receive(MafiaNet::PeerGuid peer, const std::string &body);
        void Send(MafiaNet::PeerGuid peer, nlohmann::json payload);
        /** Picked plants within sync range of one player, for their game to hide. */
        void SendPoints(MafiaNet::PeerGuid peer, std::vector<GatheringPoint> points, bool respawn = false, bool full = false, std::uint64_t syncGeneration = 0);
        void BroadcastPoints(std::uint32_t virtualWorld, std::vector<GatheringPoint> points, bool respawn);
        void FlushDeliveries();

        GatheringModule _gathering;
        std::map<std::uint64_t, std::uint32_t> _playerWorlds;
        std::map<MafiaNet::PeerGuid, std::deque<Delivery>> _outgoing;
        std::uint64_t _lastPruneMs = 0;
    };
} // namespace KCDC::Features::Gathering
