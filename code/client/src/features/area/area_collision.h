#pragma once

#include "shared/features/area/area_collision_entity.h"

namespace KCDC::Features::Area {
    class AreaCollision final: public Shared::Entities::AreaCollisionEntity {
      public:
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;
    };
} // namespace KCDC::Features::Area
