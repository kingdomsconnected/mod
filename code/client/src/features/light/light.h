#pragma once

#include "shared/features/light/light_entity.h"

#include <atomic>
#include <mafianet/get_time.h>

namespace KCDC::Features::Light {
    /** Client binding; native calls only run from the game-thread frame. */
    class Light final: public Shared::Entities::LightEntity {
      public:
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;
        void OnConstructed() override {
            _dirty = true;
        }
        void OnDeserialized(bool transformUpdated) override {
            _dirty = true;
        }
        void Frame();
        void Unbind();

      private:
        std::uint32_t _handle = 0;
        Shared::Game::LightTuning _appliedTuning;
        glm::vec3 _appliedPosition {0.0F};
        std::atomic<bool> _dirty {true};
        MafiaNet::Time _nextBuildAttempt = 0;
    };
} // namespace KCDC::Features::Light
