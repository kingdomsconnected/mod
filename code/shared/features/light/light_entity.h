#pragma once

#include "shared/features/light/light_tuning.h"
#include "shared/features/player/player_entity.h"

#include <networking/replication/network_entity.h>

namespace KCDC::Shared::Entities {
    /** A server-owned point light, streamed to late arrivals like a prop. */
    class LightEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Light";
        // Covers the largest supported light and keeps the interest-grid bound
        // independent of tuning updates.
        static constexpr float kStreamRange = 250.0F;
        Shared::Game::LightTuning tuning;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            tuning.SerializeFields(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            tuning.SerializeFields(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        }
    };
} // namespace KCDC::Shared::Entities
