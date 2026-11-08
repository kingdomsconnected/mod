#include "light_service.h"

#include "shared/pose_math.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Light {
    Shared::Entities::LightEntity *LightService::Spawn(const glm::vec3 &position, const Shared::Game::LightTuning &tuning, std::uint32_t virtualWorld) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || !Shared::Game::IsWorldPosition(position) || !tuning.IsSane()) {
            return nullptr;
        }
        auto *light = replication->CreateEntity<Shared::Entities::LightEntity>();
        if (light == nullptr) {
            return nullptr;
        }
        light->position        = position;
        light->tuning          = tuning;
        light->streaming.range = Shared::Entities::LightEntity::kStreamRange;
        light->SetVirtualWorld(virtualWorld);
        return light;
    }

    bool LightService::SetTuning(Shared::Entities::LightEntity *light, const Shared::Game::LightTuning &tuning) {
        if (light == nullptr || !tuning.IsSane()) {
            return false;
        }
        light->tuning = tuning;
        return true;
    }

    void LightService::Destroy(Shared::Entities::LightEntity *light) {
        if (light == nullptr) {
            return;
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(light);
        }
    }

} // namespace KCDC::Features::Light
