#pragma once

#include "shared/features/light/light_entity.h"

#include <cstdint>

namespace KCDC::Features::Light {
    /** Server authority for replicated point lights. Framework tracks script ownership. */
    class LightService final {
      public:
        Shared::Entities::LightEntity *Spawn(const glm::vec3 &position, const Shared::Game::LightTuning &tuning, std::uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL);
        static bool SetTuning(Shared::Entities::LightEntity *light, const Shared::Game::LightTuning &tuning);
        void Destroy(Shared::Entities::LightEntity *light);
    };
} // namespace KCDC::Features::Light
