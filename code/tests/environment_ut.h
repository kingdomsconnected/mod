#pragma once

#include "features/world/world_environment_values.h"

#include "shared/features/world/world_entity.h"

#include <limits>

MODULE(environment_values, {
    IT("normalizes hours and maps non-finite input to midnight", {
        EQUALS(KCDC::Features::World::NormalizeHour(24.0F), 0.0F);
        EQUALS(KCDC::Features::World::NormalizeHour(49.5F), 1.5F);
        EQUALS(KCDC::Features::World::NormalizeHour(-1.0F), 0.0F);
        EQUALS(KCDC::Features::World::NormalizeHour(12.25F), 12.25F);
        EQUALS(KCDC::Features::World::NormalizeHour(std::numeric_limits<float>::infinity()), 0.0F);
        EQUALS(KCDC::Features::World::NormalizeHour(std::numeric_limits<float>::quiet_NaN()), 0.0F);
    });

    IT("converts bounded game seconds without overflowing integer input", {
        EQUALS(KCDC::Features::World::BlendGameMilliseconds(0), 0LL);
        EQUALS(KCDC::Features::World::BlendGameMilliseconds(30), 30000LL);
        EQUALS(KCDC::Features::World::BlendGameMilliseconds(-10), 0LL);
        EQUALS(KCDC::Features::World::BlendGameMilliseconds(std::numeric_limits<int>::max()), 3600000LL);
        EQUALS(KCDC::Features::World::BlendGameMilliseconds(std::numeric_limits<int>::min()), 0LL);
    });

    IT("accepts a wind the engine can be handed and refuses one it cannot", {
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(0.0F)), true);
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(3.0F, -4.0F, 0.0F)), true);

        // Exactly at the cap, and just past it on one axis.
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(KCDC::Shared::Entities::WorldEntity::kMaxWindSpeed, 0.0F, 0.0F)), true);
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(KCDC::Shared::Entities::WorldEntity::kMaxWindSpeed + 1.0F, 0.0F, 0.0F)), false);

        // Under the cap on every axis, over it as a vector.
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(40.0F, 40.0F, 0.0F)), false);

        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F)), false);
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(std::numeric_limits<float>::infinity(), 0.0F, 0.0F)), false);

        // Squaring this overflows to infinity rather than comparing under.
        EQUALS(KCDC::Shared::Entities::WorldEntity::ValidWind(glm::vec3(std::numeric_limits<float>::max(), 0.0F, 0.0F)), false);
    });
});
