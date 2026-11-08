#pragma once

#include "shared/features/level_edit/level_edit_entity.h"

#include <mafianet/replica_manager3.h>

namespace KCDC::Features::LevelEdit {
    /**
     * Client binding of one server edit to an object the level placed.
     *
     * The replica carries only the intent. `LevelEditService` owns the one
     * register of the level's objects the server has changed, and applies
     * this edit to it on its tick -- the wire changes it from the peer pump,
     * where the world must not be touched -- so every callback here only
     * queues. When the replica goes, the service puts the object back.
     */
    class LevelEdit final: public Shared::Entities::LevelEditEntity {
      public:
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;
    };
} // namespace KCDC::Features::LevelEdit
